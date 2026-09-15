module;

#define FUSE_USE_VERSION 30
#define _FILE_OFFSET_BITS 64
#include <fuse.h>
#include <errno.h>

module qsefs.fuse;

namespace qsefs {

int FUSE::run() {
    const fuse_operations ops{
        .getattr =
            [] (
                const char* path,
                struct stat* stat,
                fuse_file_info* /* info */
            ) -> int {
                if (const auto st = get().getattr(path)) {
                    *stat = *st;
                    return 0;
                } else {
                    return -ENOENT;
                }
            },
        .read =
            [] (
                const char* path,
                char* buf,
                uz size,
                off_t offset,
                fuse_file_info* /* info */
            ) -> int {
                return get().read(path, { buf, size }, offset);
            },
        .readdir =
            [] (
                const char* path,
                void* buf,
                fuse_fill_dir_t filler,
                off_t /* offset */,
                fuse_file_info* /* info */,
                fuse_readdir_flags /* flags */
            ) -> int {
                if (auto entries = get().read_dir(path); entries.has_value()) {
                    filler(buf, ".", NULL, 0, FUSE_FILL_DIR_DEFAULTS);
                    filler(buf, "..", NULL, 0, FUSE_FILL_DIR_DEFAULTS);

                    for (const auto& entry : *entries) {
                        filler(
                            buf,
                            entry.c_str(),
                            NULL,
                            0,
                            FUSE_FILL_DIR_DEFAULTS
                        );
                    }
                    return 0;
                } else {
                    return -entries.error();
                }
            },
    }; // <-- ops

    std::vector<const char*> argv{};

    static const std::string type{"qsefs"};
    static const std::vector<std::string> args{
        "-f",
        // Single-threaded flag. Coward. TODO: run in multithreaded mode
        "-s",
        // Allow other users to view - it's readonly anyway
        "-o", "allow_other"
    };

    argv.push_back(type.data());
    argv.push_back(this->target_path.c_str());
    for (const auto& s : args) {
        argv.push_back(s.data());
    }

    return fuse_main(
        argv.size(),
        const_cast<char**>(argv.data()),
        &ops,
        nullptr
    );
} // <-- void FUSE::run()

} // <-- namespace qsefs
