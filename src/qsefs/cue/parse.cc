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

std::vector<Cue::Track> Cue::parse(
    std::string_view data,
    std::vector<std::pair<std::string, std::string>>& unknown_tags
) {
    std::vector<Cue::Track> ret{};

    const auto parsed_cue = parse_kv_cue(data);

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
            ret.push_back(next);
            next.start = Frames::max();
            next.end   = Frames::zero();
        }
    }; // <-- push_track()

    const std::unordered_map<std::string, TagProc, Hash, Compare> procs{
        {
            "GENRE", [&] (std::string_view g) { next.meta.genre = g; }
        },
        {
            "DATE", [&] (std::string_view y) {
                std::from_chars(y.cbegin(), y.cend(), next.meta.year);
            }
        },
        {
            "DISCID", [&] (std::string_view id) { next.meta.disc_id = id; }
        },
        {
            "COMMENT", [&] (std::string_view c) { next.meta.comment = c; }
        },
        {
            "PERFORMER", [&] (std::string_view p) { next.meta.artist = p; }
        },
        {
            "COMPOSER", [&] (std::string_view c) { next.meta.composer = c; }
        },
        {
            "ISRC", [&] (std::string_view isrc) { next.meta.isrc = isrc; }
        },
        {
            "TITLE", [&] (std::string_view t) {
                (in_track ? next.meta.title : next.meta.album) = t;
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

                std::from_chars(info.cbegin(), info.cend(), next.meta.index);
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
                                        auto sv = std::string_view{
                                            prt.begin(), prt.end()
                                        };
                                        while (sv.starts_with(' ')) {
                                            sv = sv.substr(1);
                                        }
                                        while (sv.ends_with(' ')) {
                                            sv = sv.substr(0, sv.size() - 1);
                                        }
                                        return sv;
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
                        !ret.empty()
                        && ret.back().end == Frames::zero()
                        && ret.back().file == next.file
                    ) {
                        ret.back().end = result;
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
            unknown_tags.emplace_back(k, v);
        } catch (const std::exception& e) {
            throw "Exception in CUE parser: {}"_err(e.what());
        }
    }

    push_track();
    stdr::sort(
        ret,
        std::less<>{},
        [] (auto& t) { return t.meta.index; }
    );

    return ret;
} // <-- vector<Track> Cue::parse(data)

} // <-- namespace qsefs
