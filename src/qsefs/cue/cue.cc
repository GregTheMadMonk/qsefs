module qsefs.cue;

import dxx.errors;

using namespace dxx::errors::literals;

namespace qsefs {

Cue::Cue(const stdfs::path& file) {
    std::ifstream cue{file};

    if (!cue.good()) {
        throw "Failed to open {}"_err(file);
    }

    this->data = {
        std::istreambuf_iterator<char>{cue},
        std::istreambuf_iterator<char>{}
    };

    this->tracks = parse(this->data, this->unknown_tags);

    std::println("Successfully parsed {}", file);

    for (const auto& track : this->tracks) {
        auto [ ref, is_new ] = this->base_files.emplace(
            track.file, file.parent_path() / track.file
        );

        if (!is_new) {
            continue;
        }

        std::println("Mapped new {}", track.file);
    }

    // Substitute missing track ends to be till the end of file
    for (auto& track : this->tracks) {
        if (track.end != Frames::zero()) {
            continue;
        }

        track.end = stdc::duration_cast<Frames>(
            this->base_files.at(track.file).file_info().duration
        );
    }

    // Cache the predicted file sizes
    for (auto& track : this->tracks) {
        track.file_size = this->get_track_file_size(track);
    }
} // <-- Cue::Cue(file)

uz Cue::get_track_file_size(const Track& track) const {
    using Duration = AudioInput::Info::Duration;
    const auto dur = stdc::duration_cast<Duration>(track.length());

    return this->base_files.at(track.file).wav_info().data_size_for(dur);
} // <-- uz Cue::get_track_file_size(track)

int Cue::read_track(const Track& t, std::span<char> buf, iptr off) const {
    const auto  full_size = this->get_track_file_size(t);
    auto& base            = this->base_files.at(t.file);
    const auto& wav       = base.wav_info();
    const auto  hdr       = wav.pack(full_size, t.meta);

    // TODO: Proper error codes

    // If some portion of the `buf` is supposed to be the header, read it
    if (off < 0) {
        throw "TODO"_err;
    }

    auto uoff = static_cast<uz>(off);

    if (uoff > full_size) {
        return 0;
    }

    uz buf_size = buf.size();
    if (off + buf.size() > full_size) {
        buf_size -= (off + buf.size() - full_size);
    }

    uz   cur  = 0;
    for (; uoff + cur < hdr.size() && cur < buf_size; ++cur) {
        buf[cur] = std::bit_cast<char>(hdr[uoff + cur]);
    }

    if (cur >= buf_size) {
        return buf_size;
    }

    // Header done, read the audio data
    // Offset into the WAV audio data
    const auto d_offs = uoff + cur - hdr.size();
    // Bytes per sample
    const auto bps = wav.channels * wav.bits_per_sample / 8;
    // The track's first sample index in the original file
    using Duration = AudioInput::Info::Duration;
    const auto s0  = stdc::duration_cast<Duration>(t.start).count()
                   * wav.sample_rate
                   * Duration::period::num
                   / Duration::period::den;

    // The index of the first sample that should be read in this operation
    const auto sample = s0 + d_offs / bps;
    // Bytes to skip from this first sample
    const auto sample_off = d_offs % bps;

    // Number of bytes remaining to read
    const auto bytes_to_read = buf_size - cur;
    // Number of samples to read
    // May start and end on an incomplete sample - overshoot the raw division
    // by two should be OK
    const auto samples_to_read = (bytes_to_read + bps - 1) / bps + 1;

    base.seek_sample(sample);
    const auto smp = base.read_samples(sample, samples_to_read);
    uz sample_cur = sample_off;
    for (; cur < buf_size && sample_cur < smp.size(); ++cur, ++sample_cur) {
        buf[cur] = smp[sample_cur];
    }

    // Might've encountered the end of the stream, return the number of bytes
    // read
    return cur;
} // <-- int Cue::read_track(t, buf, off)

} // <-- namespace qsefs
