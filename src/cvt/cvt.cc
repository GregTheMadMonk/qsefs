module;

#include <cmath> // prevent conflicts with `extern "C"` on GCC

extern "C" {
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/mem.h>
} // <-- extern "C"

module qsefs.cvt;

import dxx.errors;
import dxx.overload;
import dxx.utils;

using namespace dxx::errors::literals;

namespace qsefs {

namespace cvt {

std::vector<u8> WAVHeader::pack(u32 data_size) const {
    std::vector<u8> ret{};

    static_assert(riff_size + fmt_size + 8 == 44);
    // Calculate the header size
    ret.resize(riff_size + fmt_size + 8, 0);
    uz cur = 0; // "cursor"

    const auto write = dxx::overload::Overload{
        [&ret, &cur] (u8 byte) {
            ret.at(cur++) = byte;
        },
        [] <uz n> (this auto& self, const char (&str)[n]) {
            for (uz i = 0; i < n - 1; ++i) {
                self(str[i]);
            }
        },
        [] <typename T> (this auto& self, const T& v) {
            const auto buf = std::bit_cast<std::array<u8, sizeof(T)>>(v);

            for (u8 byte : buf) {
                self(byte);
            }
        },
    }; // <-- write(v)

    // riff chunk id
    write("RIFF");
    // riff chunk size
    write(u32{ riff_size - (2 * 4) + fmt_size + data_size });
    // wave id
    write("WAVE");

    // fmt chunk id
    write("fmt ");
    // fmt chunk size
    write(static_cast<u32>(fmt_size - 2 * 4));
    // format tag
    write(std::to_underlying(this->tag));

    write(this->channels);
    write(this->sample_rate);

    if (this->bits_per_sample % 8 != 0) {
        throw "Bits per sample that's not divisible by 8 is unsupported: {}"_err
              (this->bits_per_sample);
    }

    write(
        static_cast<u32>(
            this->sample_rate
            * (this->bits_per_sample / 8)
            * this->channels
        )
    ); // byte rate

    write(
        static_cast<u16>(this->channels * this->bits_per_sample / 8)
    ); // alignment

    write(this->bits_per_sample);

    write("data");
    write(data_size);

    return ret;
} // <-- vector<u8> WAVHeader::pack() const

u32 WAVHeader::data_size_for(const stdc::milliseconds& ms) const {
    // Millisecond-level deviations in audio between tracks are OK to us
    const auto samples = ms.count() * this->sample_rate / 1000;

    return samples * this->bits_per_sample * this->channels / 8;
} // <-- u32 WAVHeader::data_size_for(ms) const

} // <-- namespace cvt

static constexpr uz av_ctx_buf_size = 1024;

AudioInput::AudioInput(const stdfs::path& path)
    : file{path}
    , seek{0}
    , buf{
          [] {
              auto* ret = av_malloc(av_ctx_buf_size);
              if (ret == nullptr) {
                  throw "Cannot av_malloc a buffer of {} bytes"_err
                        (av_ctx_buf_size);
              }
              return ret;
          } ()
      }
    , io{
          [this] {
              auto ret = avio_alloc_context(
                  reinterpret_cast<u8*>(this->buf.get()),
                  av_ctx_buf_size,
                  0, // read-only
                  this,
                  &AudioInput::readf,
                  nullptr,
                  &AudioInput::seekf
              );

              if (ret == nullptr) {
                  throw "avio_alloc_context() failed"_err;
              }

              return ret;
          } ()
      }
    , fmt{
          [this] {
              auto* ret = avformat_alloc_context();
              if (ret == nullptr) {
                  throw "avformat_alloc_context() error"_err;
              }

              ret->pb = this->io.get();

              if (
                  int res = avformat_open_input(&ret, "IN", nullptr, nullptr);
                  res < 0
              ) {
                  // Frees ret on failure automatically
                  throw "error avformat_open_input()={}"_err(res);
              }

              return ret;
          } ()
      }
    , cdx{
          [this] {
              if (
                  int r = avformat_find_stream_info(this->fmt.get(), nullptr);
                  r < 0
              ) {
                  throw "error avformat_find_stream_info()={}"_err(r);
              }

              this->info.audio_stream = av_find_best_stream(
                  this->fmt.get(),
                  AVMEDIA_TYPE_AUDIO,
                  -1,
                  -1,
                  &this->codec,
                  0
              ); // <-- audio_stream

              if (this->info.audio_stream == AVERROR_STREAM_NOT_FOUND) {
                  throw "av_find_best_stream() stream not found"_err;
              }

              if (this->info.audio_stream == AVERROR_DECODER_NOT_FOUND) {
                  throw "av_find_best_stream() decoder not found"_err;
              }

              auto* ret = avcodec_alloc_context3(this->codec);

              if (ret == nullptr) {
                  throw "avcodec_alloc_context3() error"_err;
              }

              return ret;
          } ()
      }
{
    std::println("  num streams:     {}", this->fmt->nb_streams);
    std::println("  audio stream:    {}", this->info.audio_stream);

    const auto* stream = this->fmt->streams[this->info.audio_stream];
    std::println("  sample format:   {}", stream->codecpar->format);

    if (
        int r = avcodec_parameters_to_context(this->cdx.get(), stream->codecpar);
        r < 0
    ) {
        throw "avcodec_parameters_to_context() error {}"_err(r);
    }

    if (int r = avcodec_open2(this->cdx.get(), this->codec, nullptr); r < 0) {
        throw "avcodec_open2() error {}"_err(r);
    }

    const auto& ftraits = *(
        this->info.fmt_traits = cvt::get_format(
            static_cast<AVSampleFormat>(stream->codecpar->format)
        )
    );

    std::println("  format name:     {}", ftraits.name());

    if (stream->codecpar->ch_layout.order != AV_CHANNEL_ORDER_NATIVE) {
        throw "Order != AV_CHANNEL_ORDER_NATIVE unsupported, got: {}"_err
              (std::to_underlying(stream->codecpar->ch_layout.order));
    }

    using enum cvt::WAVHeader::Tag;
    this->info.wav.tag             = ftraits.is_ieee() ? IEEE : PCM;
    this->info.wav.channels        = stream->codecpar->ch_layout.nb_channels;
    this->info.wav.sample_rate     = stream->codecpar->sample_rate;
    // The source is not as important as what libav intends to give us
    this->info.wav.bits_per_sample = ftraits.bits_per_sample();

    std::println("  bits per sample: {}", this->info.wav.bits_per_sample);
    std::println("  sample rate:     {}", this->info.wav.sample_rate);
    std::println("  channels:        {}", this->info.wav.channels);

    this->info.file.duration = stdc::milliseconds{
        static_cast<uz>(this->fmt->duration) * 1000 / AV_TIME_BASE
    };
    std::println("  duration:        {}ms", this->info.file.duration.count());
} // <-- AudioInput::AudioInput()

void AudioInput::seek_sample(uz sample_idx) const {
    const auto target_ts = av_rescale_q(
        sample_idx,
        AVRational{
            .num = 1,
            .den = static_cast<int>(this->info.wav.sample_rate)
        },
        this->fmt->streams[this->info.audio_stream]->time_base
    );
    av_seek_frame(
        this->fmt.get(),
        this->info.audio_stream,
        target_ts,
        AVSEEK_FLAG_BACKWARD
    );
    avcodec_flush_buffers(this->cdx.get());
} // <-- void AudtioInput::seek_sample(sample_idx)

std::vector<u8> AudioInput::read_samples(uz idx, uz num) const {
    std::vector<u8> ret{};

    ret.reserve(
        num * this->info.wav.bits_per_sample * this->info.wav.channels / 8
    );

    Packet pk{
        [] {
            if (auto* ret = av_packet_alloc()) {
                return ret;
            }
            throw "av_packet_alloc() failed"_err;
        } ()
    }; // <-- pk

    while (av_read_frame(this->fmt.get(), pk.get()) >= 0) {
        dxx::utils::Defer unref = [&pk] { av_packet_unref(pk.get()); };

        if (pk->stream_index != this->info.audio_stream) {
            continue;
        }

        // Decode the packet
        if (auto r = avcodec_send_packet(this->cdx.get(), pk.get()); r < 0) {
            throw "avcodec_send_packet() error {}"_err(r);
        }

        Frame frame{
            [] {
                if (auto* ret = av_frame_alloc()) {
                    return ret;
                }
                throw "av_frame_alloc() failed"_err;
            } ()
        };

        for (
            int r;
            (r = avcodec_receive_frame(this->cdx.get(), frame.get())), true;
        ) {
            if (r == AVERROR_EOF) {
                // Reached the end of file. Return the remainder
                return ret;
            }

            if (r == AVERROR(EAGAIN)) {
                break;
            }

            if (r < 0) {
                throw "avcodec_receive_frame() failed: {}"_err(r);
            }

            const auto ds = av_get_bytes_per_sample(this->cdx->sample_fmt);
            if (ds <= 0) {
                throw "av_get_bytes_per_sample() failed: {}"_err(ds);
            }

            for (int i = 0; i < frame->nb_samples; ++i) {
                if (frame->pts + i < idx) {
                    continue;
                }

                for (int ch = 0; ch < this->cdx->ch_layout.nb_channels; ++ch) {
                    u8* chunk_start;
                    if (this->info.fmt_traits->is_packed()) {
                        chunk_start = frame->data[0] + ds * (this->cdx->ch_layout.nb_channels * i + ch);
                    } else {
                        chunk_start = frame->data[ch] + ds * i;
                    }
                    for (int j = 0; j < ds; ++j) {
                        ret.push_back(chunk_start[j]);
                    }
                }
                --num;
                if (num == 0) {
                    return ret;
                }
            }
        }
    }

    return ret;
} // <-- vector<u8> AudioInput::read_samples(num) const

int AudioInput::readf(void* opaque, u8* buf, int buf_size) {
    auto& in = *reinterpret_cast<AudioInput*>(opaque);

    if (in.seek == in.file.size()) {
        return AVERROR_EOF;
    }

    auto tgt = in.seek + buf_size;
    if (tgt > in.file.size()) {
        tgt = in.file.size();
        buf_size = in.file.size() - in.seek;
    }

    std::copy_n(
        std::next(in.file.data().cbegin(), in.seek),
        buf_size,
        buf
    );

    in.seek += buf_size;
    return buf_size;
} // <-- void AudioInput::readf(opaque, buf, buf_size)

iptr AudioInput::seekf(void* opaque, iptr offset, int whence) {
    auto& in = *reinterpret_cast<AudioInput*>(opaque);

    if (whence & AVSEEK_SIZE) {
        return in.file.size();
    }

    auto pos = in.seek;
    switch (whence) {
    case SEEK_SET:
        pos = offset;
        break;
    case SEEK_CUR:
        pos += offset;
        break;
    case SEEK_END:
        pos = in.file.size() + offset;
        break;
    default:
        throw "Invalid seek whence: {}"_err(whence);
    }

    if (pos < 0 || pos > in.file.size()) {
        return AVERROR(EINVAL);
    }

    return (in.seek = pos);
} // <-- iptr AudioInput::seekf(opaque, offset, whence)

} // <-- namespace qsefs
