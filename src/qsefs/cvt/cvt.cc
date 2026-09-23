module;

#include "av_headers.hh"

module qsefs.cvt;

import dxx.errors;
import dxx.utils;

using namespace dxx::errors::literals;

namespace qsefs {

static constexpr uz av_ctx_buf_size = 1024;

AudioInput::AudioInput(const stdfs::path& p)
    : path{p}
    , file{p}
    , seek{0}
    , io{
          [this] {
              auto* buf = av_malloc(av_ctx_buf_size);
              if (buf == nullptr) {
                  throw "Cannot av_malloc a buffer of {} bytes"_err
                        (av_ctx_buf_size);
              }

              // avio_alloc_context takes ownersip of buf, don't need to free
              auto ret = avio_alloc_context(
                  reinterpret_cast<u8*>(buf),
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

    const auto s_format = static_cast<AVSampleFormat>(stream->codecpar->format);

    std::println("  format name:     {}", av_get_sample_fmt_name(s_format));

    if (stream->codecpar->ch_layout.order != AV_CHANNEL_ORDER_NATIVE) {
        throw "Order != AV_CHANNEL_ORDER_NATIVE unsupported, got: {}"_err
              (std::to_underlying(stream->codecpar->ch_layout.order));
    }

    using enum cvt::WAVHeader::Tag;
    static const std::set fp_formats{
        AV_SAMPLE_FMT_FLT,
        AV_SAMPLE_FMT_DBL,
        AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_DBLP,
    }; // <-- fp_formats
    this->info.wav.tag             = fp_formats.contains(s_format) ? IEEE : PCM;
    this->info.wav.channels        = stream->codecpar->ch_layout.nb_channels;
    this->info.wav.sample_rate     = stream->codecpar->sample_rate;
    // The source is not as important as what libav intends to give us
    this->info.wav.bits_per_sample = av_get_bytes_per_sample(s_format) * 8;

    std::println("  bits per sample: {}", this->info.wav.bits_per_sample);
    std::println("  sample rate:     {}", this->info.wav.sample_rate);
    std::println("  channels:        {}", this->info.wav.channels);

    this->info.file.duration = Info::Duration{
        static_cast<uz>(this->fmt->duration) * Info::Duration::period::den
        / AV_TIME_BASE / Info::Duration::period::num
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
        // Kind of expect this to be equal to 1/sample_rate anyway
        // Do we really need this here? If the assumption is wrong,
        // there will be trouble do in the read anyway..
        this->fmt->streams[this->info.audio_stream]->time_base
    );

    const auto r = av_seek_frame(
        this->fmt.get(),
        this->info.audio_stream,
        target_ts,
        AVSEEK_FLAG_BACKWARD
    );

    if (r < 0) {
        std::println(std::cerr, "av_seek_frame() error: {}", r);
    }

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
            if (r == AVERROR_INVALIDDATA) {
                // Corrupted file, report but don't crash the program
                std::println(std::cerr, "send packet AVERROR_INVALIDDATA");
                return ret;
            }
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

        uz pts = std::numeric_limits<uz>::max();

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

            // The frame that we've seeked to has pts info (in my experiments
            // it did at least), but other frames may not have it - rely on
            // the starting pts and then just use a running counter
            if (pts == std::numeric_limits<uz>::max()) {
                pts = frame->pts;
            }

            for (int i = 0; i < frame->nb_samples; ++i) {
                if (pts++ < idx) {
                    // Skip samples in the frame that come before the requested
                    // chunk
                    continue;
                }

                for (int ch = 0; ch < this->cdx->ch_layout.nb_channels; ++ch) {
                    u8* chunk_start;
                    if (av_sample_fmt_is_planar(static_cast<AVSampleFormat>(frame->format))) {
                        chunk_start = frame->extended_data[ch] + ds * i;
                    } else {
                        chunk_start = frame->extended_data[0] + ds * (this->cdx->ch_layout.nb_channels * i + ch);
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

i64 AudioInput::seekf(void* opaque, i64 offset, int whence) {
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
