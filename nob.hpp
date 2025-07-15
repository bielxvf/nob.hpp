#pragma once

#include <mutex>
#include <vector>
#include <string>
#include <stdexcept>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <limits.h>

namespace {

std::string get_executable_path()
{
    char buf[PATH_MAX] = {0};
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len == -1) {
        throw std::runtime_error("Failed to read /proc/self/exe");
    }
    buf[len] = '\0';
    return std::string(buf);
}

}

namespace nob {

namespace fs = std::filesystem;

/* Logging */
/* TODO: Mutex shouldn't block different streams!! */
enum class LogLevel {
    Info,
    Warning,
    Error,
};

enum class Verbosity {
    Quiet,
    Quieter,
    Verbose,
};

template<typename... Args>
void log(LogLevel lvl, Args&&... args)
{
    log(std::cout, lvl, std::forward<Args>(args)...);
}

template<typename... Args>
void log(std::ostream& out, LogLevel lvl, Args&&... args)
{
    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);

    switch(lvl) {
        case LogLevel::Info: {
            out << "\e[0;34m""[NOB INFO] ""\e[0m";
        } break;
        case LogLevel::Warning: {
            out << "\e[0;33m""[NOB WARNING] ""\e[0m";
        } break;
        case LogLevel::Error: {
            out << "\e[0;31m""[NOB ERROR] ""\e[0m";
        } break;
        default: {
            out << "\e[0;35m""[NOB UNKNOWN] ""\e[0m";
        } break;
    }

    (out << ... << std::forward<Args>(args));
    out << '\n';
}

inline bool mkdir(const fs::path& path)
{
    if (fs::exists(path)) {
        if (fs::is_directory(path)) {
            log(LogLevel::Info, path, " already exists, not creating");
            return true;
        }

        return false;
    }

    return fs::create_directories(path);
}

class Cmd {
public:
    template<typename... Args>
    Cmd(Args&&... args)
    {
        add(std::forward<Args>(args)...);
    }

    ~Cmd() = default;

    template<typename... Args>
    void add(Args&&... args)
    {
        (m_command.emplace_back(std::forward<Args>(args)), ...);
    }

    void set_wd(fs::path&& path)
    {
        m_working_dir = std::move(path);
    }

    int run_sync()
    {
        log(LogLevel::Info, "Running sync: ", *this);
        std::vector<char*> argv;
        for (auto& s : m_command) {
            argv.push_back(s.data());
        }
        argv.push_back(nullptr);

        pid_t pid = fork();

        if (pid < 0) {
            throw std::runtime_error(std::string("fork() failed: ") + std::strerror(errno));
        } else if (pid == 0) {
            if (m_working_dir != ".") {
                log(LogLevel::Info, "Changing working dir to ", m_working_dir);
                fs::current_path(m_working_dir);
            }
            execvp(argv[0], argv.data());
            perror("execvp failed");
            _exit(1);
        } else {
            int status;
            waitpid(pid, &status, 0);
            return status;
        }
    }

    std::string run_sync_capture()
    {
        log(LogLevel::Info, "Running sync capture: ", *this);
        std::vector<char*> argv;
        for (auto& s : m_command) {
            argv.push_back(s.data());
        }
        argv.push_back(nullptr);

        int pipefd[2];
        if (pipe(pipefd) == -1) {
            throw std::runtime_error("pipe failed");
        }

        pid_t pid = fork();
        if (pid < 0) {
            throw std::runtime_error(std::string("fork() failed: ") + std::strerror(errno));
        } else if (pid == 0) {
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);
            execvp(argv[0], argv.data());
            perror("execvp failed");
            _exit(1);
        } else {
            close(pipefd[1]);
            std::string output;
            char buffer[4096];
            ssize_t bytes_read;
            while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
                output.append(buffer, bytes_read);
            }
            if (bytes_read == -1) {
                close(pipefd[0]);
                throw std::runtime_error("Error reading from pipe");
            }

            int status;
            waitpid(pid, &status, 0);
            return output;
        }
    }

    void reset()
    {
        m_working_dir = ".";
        m_command.clear();
    }

    friend std::ostream& operator<<(std::ostream& os, const Cmd& cmd)
    {
        for (auto s : cmd.m_command) {
            os << s << ' ';
        }
        return os;
    }

private:
    std::vector<std::string> m_command;
    fs::path m_working_dir = ".";
};

void go_rebuild_urself(int argc, char** argv, fs::path source_path)
{
    auto binary_path = get_executable_path();

    auto binary_time = fs::last_write_time(binary_path);
    auto source_time = fs::last_write_time(source_path);

    if (source_time > binary_time) {
        log(LogLevel::Info, "Rebuilding meself");
        Cmd cmd("c++", source_path, "-o", binary_path);
        if (cmd.run_sync() != 0) {
            throw std::runtime_error("Rebuild failed");
        }
        execvp(binary_path.c_str(), argv);
        perror("execvp failed");
        _exit(1);
    }
}

bool download(const std::string& url,
              std::optional<fs::path> out = std::nullopt,
              std::optional<Verbosity> v = std::nullopt)
{
    Cmd cmd("curl",
            "-L" /* Follow redirects */
    );

    if (v) {
        switch (*v) {
            case Verbosity::Verbose: { cmd.add("-v"); } break;
            case Verbosity::Quiet: { cmd.add("-s"); } break;
            default: {} break;
        }
    }

    if (out_path) {
        cmd.add("-o", out->string());
    } else {
        cmd.add("-O"); /* Save to whatever name is in the Content-Disposition header or the last part of the URL */
    }
    cmd.add(url);

    return cmd.run_sync() == 0;
}

bool extract_tar_gz(const fs::path& archive,
                    std::optional<fs::path> out = std::nullopt,
                    std::optional<Verbosity> v = std::nullopt)
{
    Cmd cmd("tar",
            "-x",
            "-z"
    );

    if (v) {
        switch (*v) {
            case Verbosity::Verbose: { cmd.add("-v"); } break;
            default: {} break;
        }
    }

    cmd.add("-f", archive.string());

    if (out) {
        cmd.add("-C", out->string());
    }

    return cmd.run_sync() == 0;
}

bool extract_tar_bz2(const fs::path& archive,
                     std::optional<fs::path> out = std::nullopt,
                     std::optional<Verbosity> v = std::nullopt)
{
    Cmd cmd("tar",
            "-x", /* Extract */
            "-j"  /* Use bz2 decompression */
    );

    if (v) {
        switch (*v) {
            case Verbosity::Verbose: { cmd.add("-v"); } break;
            default: {} break;
        }
    }

    cmd.add("-f", archive.string());

    if (out) {
        cmd.add("-C", out->string());
    }

    return cmd.run_sync() == 0;
}

bool extract_bz2(const fs::path& compressed,
                 std::optional<fs::path> out = std::nullopt,
                 std::optional<Verbosity> v = std::nullopt)
{
    Cmd cmd("bzip2",
            "-d"); /* Decompress */

    if (out) {
        cmd.add("-c"); /* To stdout */
    }

    cmd.add("-k", compressed.string()); /* Keep original */

    if (v) {
        switch (*v) {
            case Verbosity::Verbose: { cmd.add("-v"); } break;
            case Verbosity::Quiet: { cmd.add("-q"); } break;
            default: {} break;
        }
    }

    if (out) {
        cmd.add(">", out->string()); /* Redirect to chosen file */
    }

    return cmd.run_sync() == 0;
}

bool extract_zip(const fs::path& archive,
                 std::optional<fs::path> out = std::nullopt,
                 std::optional<Verbosity> v = std::nullopt)
{
    Cmd cmd("unzip");

    if (v) {
        switch (*v) {
            case Verbosity::Verbose: { cmd.add("-v"); } break;
            case Verbosity::Quiet: { cmd.add("-q"); } break;
            case Verbosity::Quieter: { cmd.add("-qq"); } break;
            default: {} break;
        }
    }

    cmd.add(archive.string());

    if (out) {
            cmd.add("-d", out->string()); /* Redirect to chosen file */
    }

    return cmd.run_syns() == 0;
}

bool extract_gz(const fs::path& compressed,
                std::optional<fs::path> out = std::nullopt,
                std::optional<Verbosity> v = std::nullopt)
{
    Cmd cmd("gunzip");

    if (v) {
        switch (*v) {
            case Verbosity::Verbose: { cmd.add("-v"); } break;
            case Verbosity::Quiet: { cmd.add("-q"); } break;
            default: {} break;
        }
    }

    cmd.add("-k"); /* Keep original */

    if (out) {
            "-c", /* Redirect to stdout */
    }

    cmd.add(compressed.string());

    if (out) {
            cmd.add(">", out->string()); /* Redirect to chosen file */
    }
    return cmd.run_sync() == 0;
}

bool extract(const fs::path& in,
             std::optional<fs::path> out = std::nullopt,
             std::optional<Verbosity> v = std::nullopt)
{
    /* Detect from extensions? or metadata? */

    fs::path output = out.value_or(in.stem());

    if (in.extension() == ".gz") {
        fs::path no_gz = in.stem();
        if (no_gz.extension() == ".tar") {
            output = output.stem();
            extract_tar_gz(in, output);
        } else {
            extract_gz(in, output);
        }
    } else if (in.extension() == ".zip") {
        extract_zip(in, output);
    } else if (in.extension() == ".bz2") {
        fs::path no_bz2 = in.stem();
        if (no_bz2.extension() == ".tar") {
            extract_tar_bz2(in, output);
        } else {
            extract_bz2(in, output);
        }
    }
}

bool download_and_extract(const std::string& url, std::optional<fs::path> out = std::nullopt)
{
    throw std::runtime_error("TODO download_and_extract");
}

}
