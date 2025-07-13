/* Example nob.cpp build script */

#include "nob.hpp"

namespace fs = std::filesystem;

int main(int argc, char** argv)
{
    nob::go_rebuild_urself(argc, argv, __FILE__);

    fs::path build_dir("build");
    fs::path source_dir("src");
    fs::path source_file("test.cpp");
    fs::path output_file("test");

    nob::mkdir(build_dir);
    nob::Cmd cmd("c++",
            "-Wall", "-Wextra",
            source_dir / source_file,
            "-o",
            build_dir / output_file);
    cmd.run_sync();

    source_file = "test2.cpp";
    output_file = "test2";

    nob::mkdir(build_dir);
    cmd.reset();
    cmd.add("c++",
            "-Wall", "-Wextra",
            source_dir / source_file,
            "-o",
            build_dir / output_file);
    cmd.run_sync();
}
