#include "audio_convert.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char ** environ;
#endif

namespace meitte {
bool decode_audio_ffmpeg(const std::vector<uint8_t> & input,
                         const std::string & bin_dir,
                         int sample_rate,
                         size_t max_bytes,
                         const std::function<bool()> & cancelled,
                         std::vector<float> & output,
                         std::string & error) {
    output.clear();
    if (input.empty() || sample_rate <= 0 || max_bytes < sizeof(float)) {
        error = "invalid audio input, sample rate, or byte limit";
        return false;
    }
#ifdef _WIN32
    error = "FFmpeg audio conversion on Windows is not available; supply WAV, MP3, or FLAC";
    return false;
#else
    // A private temporary input prevents a full stdin pipe from blocking stdout drainage.
    struct FileCloser {
        void operator()(FILE * value) const {
            if (value) std::fclose(value);
        }
    };
    std::unique_ptr<FILE, FileCloser> file(std::tmpfile());
    if (!file || std::fwrite(input.data(), 1, input.size(), file.get()) != input.size() ||
        std::fflush(file.get()) != 0 || std::fseek(file.get(), 0, SEEK_SET) != 0) {
        error = "cannot stage audio for FFmpeg";
        return false;
    }
    int pipe_fd[2];
    if (pipe(pipe_fd) != 0) {
        error = "cannot create FFmpeg output pipe";
        return false;
    }
    struct Pipe {
        int read_fd, write_fd;
        ~Pipe() {
            close(read_fd);
            if (write_fd >= 0) close(write_fd);
        }
    } pipe_owner{pipe_fd[0], pipe_fd[1]};
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        error = "cannot initialize FFmpeg process";
        return false;
    }
    struct Actions {
        posix_spawn_file_actions_t & value;
        ~Actions() { posix_spawn_file_actions_destroy(&value); }
    } actions_owner{actions};
    int rc = posix_spawn_file_actions_adddup2(&actions, fileno(file.get()), STDIN_FILENO);
    rc |= posix_spawn_file_actions_adddup2(&actions, pipe_fd[1], STDOUT_FILENO);
    rc |= posix_spawn_file_actions_addclose(&actions, pipe_fd[0]);
    rc |= posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    if (rc) {
        error = "cannot configure FFmpeg process";
        return false;
    }
    std::string executable = bin_dir.empty() ? "ffmpeg" : bin_dir + "/ffmpeg";
    std::string rate = std::to_string(sample_rate);
    // Only the input/output pipes are valid protocols. Container references cannot open files or URLs.
    std::vector<std::string> args{executable, "-nostdin", "-v",     "error", "-protocol_whitelist",
                                  "pipe",     "-i",       "pipe:0", "-map",  "0:a:0",
                                  "-vn",      "-ac",      "1",      "-ar",   rate,
                                  "-f",       "f32le",    "pipe:1"};
    std::vector<char *> argv;
    for (auto & arg : args)
        argv.push_back(arg.data());
    argv.push_back(nullptr);
    pid_t pid;
    rc = posix_spawnp(&pid, executable.c_str(), &actions, nullptr, argv.data(), environ);
    if (rc) {
        error = "cannot start FFmpeg: " + std::string(std::strerror(rc));
        return false;
    }
    struct Child {
        pid_t pid;
        ~Child() {
            if (pid > 0) {
                kill(pid, SIGKILL);
                while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {
                }
            }
        }
    } child{pid};
    close(pipe_owner.write_fd);
    pipe_owner.write_fd = -1;
    std::vector<uint8_t> bytes;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    for (;;) {
        if ((cancelled && cancelled()) || std::chrono::steady_clock::now() >= deadline) {
            error = "audio conversion cancelled or exceeded 30 seconds";
            return false;
        }
        pollfd descriptor{pipe_fd[0], POLLIN, 0};
        rc = poll(&descriptor, 1, 100);
        if (rc < 0 && errno == EINTR) continue;
        if (rc < 0) {
            error = "FFmpeg output poll failed";
            return false;
        }
        if (!rc) continue;
        uint8_t buffer[16384];
        ssize_t n = read(pipe_fd[0], buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) {
            error = "FFmpeg output read failed";
            return false;
        }
        if (!n) break;
        if ((size_t) n > max_bytes - bytes.size()) {
            error = "decoded audio byte limit exceeded";
            return false;
        }
        bytes.insert(bytes.end(), buffer, buffer + n);
    }
    int status = 0;
    while ((rc = waitpid(pid, &status, WNOHANG)) == 0) {
        if ((cancelled && cancelled()) || std::chrono::steady_clock::now() >= deadline) {
            error = "audio conversion cancelled or timed out";
            return false;
        }
        poll(nullptr, 0, 10);
    }
    if (rc < 0) {
        error = "cannot collect FFmpeg exit status";
        return false;
    }
    child.pid = -1;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || bytes.empty() || bytes.size() % 4) {
        error = "FFmpeg could not decode the audio";
        return false;
    }
    output.resize(bytes.size() / 4);
    for (size_t i = 0; i < output.size(); ++i) {
        const uint8_t * b = bytes.data() + 4 * i;
        uint32_t value = uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
        static_assert(sizeof(float) == sizeof(value));
        std::memcpy(&output[i], &value, sizeof(value));
    }
    return true;
#endif
}
} // namespace meitte
