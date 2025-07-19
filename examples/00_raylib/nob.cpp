#include "nob.hpp"

using namespace nob;
fs::path build_dir = "build";
fs::path app_name = "00_raylib";

int build_app()
{
    fs::path raylib = "raylib-5.0";
    std::vector<fs::path> sources = { "src/main.cpp" };
    mkdir(build_dir);

    // Compile raylib static library
    info("Building raylib static library...");
    cd(build_dir);
    {
        std::string raylib_url = "https://github.com/raysan5/raylib/archive/refs/tags/5.0.tar.gz";

        info("Downloading and extracting raylib...");
        if (!download_and_extract(raylib_url, ".", Verbosity::Verbose)) {
            error("Could not download or extract raylib");
            return 1;
        }
        mkdir(raylib / "build");
        Cmd cmd("cmake", "..", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5");
        cmd.set_wd(raylib / "build");
        if (cmd.run_sync() != 0) {
            error("Failed to run cmake for raylib");
            return 1;
        }
        cmd.reset();
        cmd.add("make");
        cmd.set_wd(raylib / "build");
        if (cmd.run_sync() != 0) {
            error("Failed to run make for raylib");
            return 1;
        }
    }
    cd(get_project_root());

    // Build main app
    info("Building app...");
    {
        fs::path app_executable = build_dir / app_name;
        Cmd app_build("c++", "-std=c++17", "-O2", "-o", app_executable);
        for (auto s : sources) {
            app_build.add(s);
        }

        fs::path raylib_lib = build_dir / raylib / "build" / "raylib" / "libraylib.a";
        app_build.add(raylib_lib.string());
        app_build.add("-I", build_dir / raylib / "build" / "raylib" / "include");

        // Linker dependencies (adjust for your OS)
        app_build.add("-lm", "-ldl", "-lpthread", "-lGL", "-lrt", "-lX11");

        if (app_build.run_sync() != 0) {
            error("Failed to compile app.");
            return 1;
        }
        info("App build completed!");
        info("Executable: ", app_executable);
    }

    return 0;
}

int clean() {
    remove_recursive(build_dir);
    return 0;
}

int main(int argc, char** argv) {

    go_rebuild_urself(argc, argv, __FILE__);

    if (argc < 2) {
        error("Need subcommand");
    }

    std::vector<std::string> args(argc);

    for (std::size_t i = 0; i < argc; i++) {
        args[i] = std::string(argv[i]);
    }

    if (args[1] == "build") {
        return build_app();
    } else if (args[1] == "clean") {
        return clean();
    }


    return 0;
}
