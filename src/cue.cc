module qsefs.cue;

import dxx.errors;
import dxx.utils;

using namespace dxx::errors::literals;

namespace qsefs {

static constexpr auto* digits = "0123456789";

static std::string_view unquote(std::string_view sv) {
    if (sv.starts_with('"') && sv.ends_with('"')) {
        return sv.substr(1, sv.size() - 2);
    }
    return sv;
} // <-- string_view unquote(sv)

static auto parse_kv_cue(std::string_view data) {
    uz tag_off  = 0;
    uz tag_size = 0;
    uz val_off  = 0;
    uz val_size = 0;

    bool reading_tag = true;

    std::vector<std::pair<std::string_view, std::string_view>> ret;

    const auto tag = [&] { return data.substr(tag_off, tag_size); };
    const auto val = [&] { return data.substr(val_off, val_size); };

    for (uz i = 0; i < data.size(); ++i) {
        const char c = data[i];

        if (c == '\r') {
            // Ignore Windows shennanigans
            continue;
        }

        if (c == '\n') {
            if (tag_size != 0) {
                ret.emplace_back(tag(), val());
            }
            tag_off     = i + 1;
            tag_size    = 0;
            val_size    = 0;
            reading_tag = true;
            continue;
        }

        if (reading_tag && c == ' ') {
            if (tag() != "REM" && tag_size != 0) {
                reading_tag = false;
                val_off     = i + 1;
            } else {
                tag_off  = i + 1;
                tag_size = 0;
            }

            continue;
        }

        ++(reading_tag ? tag_size : val_size);
    }

    return ret;
} // <-- auto parse_kv_cue(data)

Cue::Cue(const stdfs::path& file) : path{file}, base_files{} {
    std::ifstream cue{this->path};

    if (!cue.good()) {
        throw "Failed to open {}"_err(this->path);
    }

    this->data = {
        std::istreambuf_iterator<char>{cue},
        std::istreambuf_iterator<char>{}
    };

    const auto parsed_cue = parse_kv_cue(this->data);

    using TagProc = std::function<void(std::string_view)>;

    Track next{ .end = Frames::zero() };

    bool in_track = false;

    struct Hash {
        using is_transparent = std::true_type;

        [[nodiscard]] static auto operator()(std::string_view sv)
        { return std::hash<std::string_view>{}(sv); }

        [[nodiscard]] static auto operator()(const std::string& s)
        { return std::hash<std::string>{}(s); }
    }; // <-- struct Hash

    struct Compare {
        using is_transparent = std::true_type;

        [[nodiscard]]
        static bool operator()(const std::string& a, const std::string& b)
        { return a == b; }

        [[nodiscard]]
        static bool operator()(const std::string& a, std::string_view b)
        { return a == b; }

        [[nodiscard]]
        static bool operator()(std::string_view a, const std::string& b)
        { return a == b; }
    }; // <-- struct Compare

    const auto push_track = [&] {
        if (in_track) {
            this->tracks.push_back(next);
            next.start = Frames::max();
            next.end   = Frames::zero();
        }
    }; // <-- push_track()

    const std::unordered_map<std::string, TagProc, Hash, Compare> procs{
        {
            "GENRE", [&] (std::string_view g) { next.genre = g; }
        },
        {
            "DATE", [&] (std::string_view y) {
                std::from_chars(y.cbegin(), y.cend(), next.year);
            }
        },
        {
            "DISCID", [&] (std::string_view id) { next.disc_id = id; }
        },
        {
            "COMMENT", [&] (std::string_view c) { next.comment = c; }
        },
        {
            "PERFORMER", [&] (std::string_view p) { next.artist = p; }
        },
        {
            "COMPOSER", [&] (std::string_view c) { next.composer = c; }
        },
        {
            "ISRC", [&] (std::string_view isrc) { next.isrc = isrc; }
        },
        {
            "TITLE", [&] (std::string_view t) {
                (in_track ? next.title : next.album) = t;
            }
        },
        {
            "FILE", [&] (std::string_view f) {
                if (!f.ends_with(" WAVE")) {
                    throw "Only WAVE files are supported, got: `{}`"_err
                          (f);
                }
                push_track();
                in_track = false;
                next.file = unquote(f.substr(0, f.size() - 5));
            }
        },
        {
            "TRACK", [&] (std::string_view info) {
                push_track();

                in_track = true;

                const auto spos = info.find(' ');
                if (spos == std::string_view::npos) {
                    throw "`{}`: no track type"_err(info);
                }

                const auto type = info.substr(spos + 1);
                if (type != "AUDIO") {
                    throw
                        "Only AUDIO tracks are supported, got: `{}`"_err(type);
                }

                info = info.substr(0, spos);
                if (info.find_first_not_of(digits) != std::string_view::npos) {
                    throw "Track number isn't an integer: `{}`"_err(info);
                }

                std::from_chars(info.cbegin(), info.cend(), next.index);
            }
        },
        {
            "INDEX", [&] (std::string_view ind) {
                if (!in_track) {
                    throw "INDEX not in TRACK!"_err;
                }

                const auto spos = ind.find(' ');
                if (spos == std::string_view::npos) {
                    throw "Wrong INDEX format: `{}`"_err(ind);
                }

                const auto index = ind.substr(0, spos);
                if (index.find_first_not_of(digits) != std::string_view::npos) {
                    throw "Index `{}` is not an integer"_err(index);
                }

                const auto point = ind.substr(spos + 1);
                auto p_parsed = stdv::split(point, ':')
                              | stdv::transform(
                                    [] (auto prt) {
                                        return std::string_view{
                                            prt.begin(), prt.end()
                                        };
                                    }
                                )
                              | dxx::utils::as<std::vector>;

                if (p_parsed.size() != 3) {
                    throw "Wrong index format, expected 3 parts: `{}`"_err
                          (p_parsed);
                }

                auto result = Frames::zero();
                for (auto [ i, p ] : stdv::enumerate(p_parsed)) {
                    if (p.find_first_not_of(digits) != std::string_view::npos) {
                        throw "Cannot parse time point: `{}` in `{}`"_err
                              (p, p_parsed);
                    }

                    uz val;
                    std::from_chars(p.cbegin(), p.cend(), val);

                    switch (i) {
                    case 0:
                        result += stdc::minutes(val);
                        break;
                    case 1:
                        result += stdc::seconds(val);
                        break;
                    case 2:
                        result += Frames(val);
                        break;
                    }
                }

                if (index == "00" || index == "01") {
                    next.start = std::min(next.start, result);

                    if (
                        !this->tracks.empty()
                        && this->tracks.back().end == Frames::zero()
                        && this->tracks.back().file == next.file
                    ) {
                        this->tracks.back().end = result;
                    }
                } else {
                    next.end = std::max(next.end, result);
                }
            },
        },
    }; // <-- procs

    for (const auto& [ k, v ] : parsed_cue) {
        try {
            procs.at(k)(unquote(v));
        } catch (const std::out_of_range& e) {
            throw "Unknown CUE tag: `{}` (`{}`)"_err(k, v);
        } catch (const std::exception& e) {
            throw "Exception in CUE parser: {}"_err(e.what());
        }
    }

    push_track();
    stdr::sort(this->tracks, std::less<>{}, &Track::index);

    std::println("Successfully parsed {}", this->path);

    for (const auto& track : this->tracks) {
        auto [ ref, is_new ] = this->base_files.emplace(
            track.file, this->path.parent_path() / track.file
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
    const auto ms = stdc::duration_cast<stdc::milliseconds>(track.length());

    return this->base_files.at(track.file).wav_info().data_size_for(ms);
} // <-- uz Cue::get_track_file_size(track)

int Cue::read_track(const Track& t, std::span<char> buf, iptr off) const {
    const auto  full_size = this->get_track_file_size(t);
    auto& base            = this->base_files.at(t.file);
    const auto  hdr       = base.wav_info().pack(full_size);

    for (uz i = 0; i < buf.size(); ++i) {
        const uz pos = off + i;
        if (pos < hdr.size()) {
            // Read the spoofed header at the start
            buf[i] = std::bit_cast<char>(hdr[pos]);
            continue;
        }

        // Read the demuxed audio data
        // First, calculate the offset into the data segment of the WAV
        const auto d_offs = pos - hdr.size();
        // Bytes per sample
        const auto bps = base.wav_info().channels
                       * base.wav_info().bits_per_sample / 8;

        // Recalculate the start sample
        const auto s0 = 
            stdc::duration_cast<stdc::milliseconds>(t.start).count()
            * base.wav_info().sample_rate / 1000;

        // Get sample index
        const auto sample     = s0 + d_offs / bps;
        // Get offset into the first sample
        const auto sample_off = d_offs % bps;
        // Get num samples to read (read the partial sample too if the read
        // call buffer boundary is inside of a sample)
        const auto bytes_to_read   = buf.size() - i;
        const auto samples_to_read = (bytes_to_read + bps - 1) / bps;

        base.seek_sample(sample);

        const auto smp = base.read_samples(sample, samples_to_read);
        const auto start = i;
        while (i < buf.size() && (i - start + sample_off) < smp.size()) {
            buf[i] = smp[i - start + sample_off];
            ++i;
        }

        while (i < buf.size()) {
            buf[i] = 0;
        }

        break;
    }

    return buf.size();
} // <-- int Cue::read_track(t, buf, off)

} // <-- namespace qsefs
