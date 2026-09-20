module;

#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

module qsefs.fuse;

import dxx.errors;

using namespace dxx::errors::literals;
using namespace std::literals;

namespace qsefs {

static void check_dir_exists(const stdfs::path& p) {
    if (!stdfs::exists(p) || !stdfs::is_directory(p)) {
        throw "Directory doesn't exist: {}"_err(p);
    }
} // <-- void check_dir_exists(p)

[[nodiscard]]
static bool is_ext(const stdfs::path& p, std::string_view ext) {
    return stdr::equal(
        p.extension().string(),
        ext,
        [] (char a, char b) { return std::tolower(a) == std::tolower(b); }
    );
} // <-- is_ext(p, ext)

[[nodiscard]]
static bool is_cue(const stdfs::path& p) {
    return stdfs::is_regular_file(p) && is_ext(p, ".cue");
} // <-- bool is_cue(p)

[[nodiscard]]
static bool is_img(const stdfs::path& p) {
    static constexpr std::array img_exts{ ".jpg"sv, ".jpeg"sv, ".png"sv };
    return stdr::any_of(img_exts, [&p] (auto e) { return is_ext(p, e); });
} // <-- bool is_img(p)

FUSE& FUSE::get() {
    static FUSE inst{};
    return inst;
} // <-- FUSE& FUSE::get()

void FUSE::set_source(const stdfs::path& p) {
    check_dir_exists(p);
    this->source_path = p;
} // <-- void FUSE::set_source(p)

void FUSE::set_target(const stdfs::path& p) {
    check_dir_exists(p);
    this->target_path = p;
} // <-- void FUSE::set_target(p)

std::expected<std::vector<FUSE::DirEntry>, int>
FUSE::read_dir(const stdfs::path& dir) const {
    const auto path = this->real_path(dir);

    if (!path.has_value()) {
        return std::unexpected(ENOENT);
    }

    if (is_cue(*path)) {
        std::vector<DirEntry> ret{};

        // Text contents of the CUE sheet
        ret.push_back("text");

        if (const auto& cue = this->read_cue(*path)) {
            for (const auto& track : cue->get_tracks()) {
                ret.push_back(
                    std::format(
                        "{:02} - {}.wav", track.meta.index, track.meta.title
                    )
                );
                for (auto& c : ret.back()) {
                    if (c == '/') {
                        c = '|';
                    }
                }
            }
        } else {
            ret.push_back("error");
        }

        // Alias images from the parent directory
        for (const auto& e : stdfs::directory_iterator(path->parent_path())) {
            const auto& p = e.path();
            if (is_img(p)) {
                ret.push_back(p.filename().string());
            }
        }

        return ret;
    }

    if (!stdfs::is_directory(*path)) {
        return std::unexpected(ENOTDIR);
    }

    std::expected<std::vector<DirEntry>, int> ret;
    auto& v = ret.emplace();

    for (const auto& e : stdfs::directory_iterator(*path)) {
        const auto& path = e.path();

        if (!this->show_base_files) {
            // Need to load all possible .cue's in the folder
            // to know what files to hide
            // TODO: make the analysis more shallow without loading the whole
            //       base audio file
            if (is_cue(path)) {
                if (const auto& cue = this->read_cue(path); cue.has_value()) {
                    for (const auto& file : cue->get_base_files()) {
                        this->hidden_files.insert(file);
                    }
                }
            }
        }

        auto str = path.filename().string();

        if (this->ignore_dotfiles && str.starts_with('.')) {
            continue;
        }

        v.push_back(std::move(str));
    }

    std::erase_if(
        v, [this, &path] (auto& e) {
            return this->hidden_files.contains(*path / e);
        }
    );

    return ret;
} // <-- optional<vector<DirEntry>> FUSE::read_dir(dir) const

const FUSE::CueCacheEntry::Data&
FUSE::read_cue(const stdfs::path& cue) const {
    this->flush_cache();

    const auto now = stdc::steady_clock::now();
    try {
        auto [ it, _ ] = this->cache.try_emplace(cue.string(), now, cue);
        it->second.fetched = now;
    } catch (const std::exception& e) {
        auto [ it, _ ] = this->cache.try_emplace(cue.string(), now, std::unexpected(e.what()));
        it->second.fetched = now;
    }

    return this->cache.at(cue.string()).data;
} // <-- FUSE::read_cue(cue) const

std::optional<const Cue::Track&>
FUSE::get_track(const stdfs::path& path, const stdfs::path& cue_path) const {
    if (!is_ext(path, ".wav")) {
        // All of our tracks are emulated as WAV
        return std::nullopt;
    }

    // Get track number - all tracks we provide have the first
    // two characters of their names denote a valid track index
    //
    // Consider, however, that the path might be bogus because
    // of e.g. user typo
    const auto name = path.filename().string();
    if (
        name.size() < 2
        || !std::isdigit(name.at(0))
        || !std::isdigit(name.at(1))
    ) {
        // Bogus file, the user should return ENOENT when they see it
        return std::nullopt;
    }

    const uz idx = (name.at(0) - '0') * 10 + (name.at(1) - '0') - 1;
    auto& cue = this->read_cue(cue_path);

    if (!cue.has_value() || idx >= cue->get_tracks().size()) {
        // A wild error appears
        return std::nullopt;
    }

    return cue->get_tracks().at(idx);
} // <-- optional<Track> get_track(path, parent)

std::optional<struct stat> FUSE::getattr(const stdfs::path& p) const {
    if (const auto path = this->real_path(p)) {
        struct stat ret{
            .st_nlink = 1,
            .st_mode  = 0444,
            .st_uid   = getuid(),
            .st_gid   = getgid(),
        }; // <-- ret

        if (stdfs::is_directory(*path) || is_cue(*path)) {
            ret.st_mode |= S_IFDIR | 0111;
            ++ret.st_nlink;
        } else if (auto parent = path->parent_path(); is_cue(parent)) {
            // In the virtual .cue folder
            // Does not contain subfolders
            ret.st_mode |= S_IFREG;

            if (path->filename() == "error") {
                // .cue parse error
                if (auto& cue = this->read_cue(parent); !cue.has_value()) {
                    ret.st_size = cue.error().size();
                } else {
                    // Error disappeared for some reason
                    return std::nullopt;
                }
            } else if (path->filename() == "text") {
                stat(path->parent_path().c_str(), &ret);
                ret.st_mode = S_IFREG | 0444;
            } else if (is_img(*path)) {
                // Images from the parent folder of a .cue are aliased
                const auto img_path = parent.parent_path() / path->filename();
                if (
                    stdfs::is_regular_file(img_path)
                    || stdfs::is_symlink(img_path)
                ) {
                    ret.st_size = stdfs::file_size(img_path);
                } else {
                    return std::nullopt;
                }
            } else if (auto track = this->get_track(*path, parent)) {
                ret.st_size = track->file_size;
                ret.st_mode |= S_IFREG;
            } else {
                // Unexpected file - there should not be a file that falls
                // through to here. Don't throw, returning nullopt will result
                // in a ENOENT
                // Or just another error
                return std::nullopt;
            }
        } else {
            ret.st_mode |= S_IFREG;
            ret.st_size = stdfs::file_size(*path);
        }

        return ret;
    } else {
        return std::nullopt;
    }
} // <-- optional<stat> FUSE::getattr(p) const

int FUSE::read(const stdfs::path& p, std::span<char> buf, off_t offset) const {
    if (auto path = this->real_path(p)) {
        // Try to handle .cue virtual folder first
        if (auto parent = path->parent_path(); is_cue(parent)) {
            // .cue parse error
            if (path->filename() == "error") {
                if (auto& cue = this->read_cue(parent); !cue.has_value()) {
                    auto& err = cue.error();

                    uz i = 0;
                    for (; i < std::min(buf.size(), err.size()); ++i) {
                        buf[i] = err[i];
                    }
                    return i;
                } else {
                    return -ENOENT;
                }
            }

            if (path->filename() == "text") {
                // Raw .cue data
                path = path->parent_path();
            } else if (is_img(*path)) {
                // Images are aliased from the parent
                path = parent.parent_path() / path->filename();
            } else if (auto track = this->get_track(*path, parent)) {
                if (auto& cue = this->read_cue(parent); !cue.has_value()) {
                    return -ENOENT;
                } else {
                    return cue->read_track(*track, buf, offset);
                }
            } else {
                // Bogus file or track error
                return -ENOENT;
            }
        }

        std::ifstream file{*path, std::ios::binary};

        if (!file.good()) {
            std::println(std::cerr, "READ {} !file.good()", *path);
            return -EIO;
        }

        file.seekg(offset);

        if (file.fail()) {
            std::println(std::cerr, "READ {} file.fail()", *path);
            return -EIO;
        }

        file.read(buf.data(), buf.size());

        if (const int ret = file.gcount(); ret > 0) {
            return ret;
        }

        std::println(std::cerr, "READ {} got 0 bytes", *path);
        return -EIO;
    } else {
        return -ENOENT;
    }
} // <-- int FUSE::read(p, buf) const

std::optional<stdfs::path> FUSE::real_path(const stdfs::path& p) const {
    auto path = this->source_path / p.relative_path();
    if (stdfs::is_symlink(path)) {
        path = stdfs::read_symlink(path);
    }

    if (stdfs::is_regular_file(path) || stdfs::is_directory(path)) {
        return path;
    }

    // Virtual .cue directory
    if (is_cue(path.parent_path())) {
        return path;
    }

    return std::nullopt;
} // <-- optional<path> FUSE::real_path(p) const

void FUSE::flush_cache() const {
    while (this->cache.size() > this->max_cached_files) {
        auto oldest = this->cache.begin();
        for (auto it = this->cache.begin(); it != this->cache.end(); ++it) {
            if (it->second.fetched < oldest->second.fetched) {
                oldest = it;
            }
        }

        std::println(std::cerr, "Removing {} from cache", oldest->first);
        this->cache.erase(oldest);
    }
} // <-- void FUSE::flush_cache() const

} // <-- namespace qsefs
