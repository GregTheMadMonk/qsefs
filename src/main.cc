import dxx.errors;
import qsefs.fuse;
import stl;

using namespace dxx::errors::literals;

int main(int argc, char** argv) {
    const auto usage = [prog_name=argv[0]] {
        std::println(
            std::cerr,
            "Usage: {} <source_dir> <target_dir> [FUSE arguments...]",
            prog_name
        );
    }; // <-- usage()

    try {
        if (argc < 3) {
            usage();
            throw "Too few arguments"_err;
        }

        auto& fuse = qsefs::FUSE::get();

        fuse.set_source(argv[1]);
        fuse.set_target(argv[2]);

        // TODO: get from config
        fuse.set_ignore_dotfiles(true);
        fuse.set_cache_size(50);
        fuse.set_show_base_files(false);

        return fuse.run(std::span{argv + 3, static_cast<uz>(argc) - 3});
    } catch (const std::exception& e) {
        std::println(std::cerr, "ERROR: {}", e.what());
        return EXIT_FAILURE;
    }
} // <-- int main(argc, argv)
