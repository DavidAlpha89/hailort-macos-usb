/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/

#include "debug_shell.hpp"

#include "common/file_descriptor.hpp"
#include "common/utils.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

namespace hailort
{

namespace
{

constexpr auto DEBUG_SHELL_TOKEN_PATH = "/etc/hailort-debug-shell.token";
constexpr size_t MIN_TOKEN_LENGTH = 32;
constexpr size_t MAX_TOKEN_LENGTH = 96;
constexpr size_t MAX_TOKEN_FILE_BYTES = 4096;
// HRPC request protobuf buffers are 256 bytes. Token + command/input + metadata must fit.
constexpr size_t MAX_COMMAND_LENGTH = 128;
constexpr uint32_t MAX_TIMEOUT_MS = 30000;
// HRPC has a 2048-byte body limit. Keep protobuf replies and requests safely below it.
constexpr uint32_t MAX_OUTPUT_BYTES = 1536;
constexpr size_t MAX_INTERACTIVE_INPUT_BYTES = 128;
constexpr uint32_t MAX_INTERACTIVE_POLL_MS = 1000;
constexpr uint32_t MAX_TERMINAL_DIMENSION = 1000;
constexpr int POLL_INTERVAL_MS = 50;

Expected<std::string> read_expected_token()
{
    const auto raw_fd = open(DEBUG_SHELL_TOKEN_PATH, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(raw_fd >= 0, HAILO_INVALID_OPERATION, "Debug shell token file is unavailable");
    FileDescriptor token_fd(raw_fd);

    struct stat file_info = {};
    CHECK(0 == fstat(token_fd, &file_info), HAILO_INVALID_OPERATION, "Failed to inspect debug shell token file");
    CHECK(S_ISREG(file_info.st_mode), HAILO_INVALID_OPERATION, "Debug shell token path is not a regular file");
    CHECK((0 == file_info.st_uid) && (0 == (file_info.st_mode & (S_IRWXG | S_IRWXO))), HAILO_INVALID_OPERATION,
        "Debug shell token file must be owned by root and inaccessible to group/other users");
    CHECK((file_info.st_size > 0) && (file_info.st_size <= static_cast<off_t>(MAX_TOKEN_FILE_BYTES)),
        HAILO_INVALID_OPERATION, "Debug shell token file size is invalid");

    std::string token(static_cast<size_t>(file_info.st_size), '\0');
    size_t offset = 0;
    while (offset < token.size()) {
        const auto bytes_read = read(token_fd, &token[offset], token.size() - offset);
        CHECK(bytes_read > 0, HAILO_INVALID_OPERATION, "Failed reading debug shell token file");
        offset += static_cast<size_t>(bytes_read);
    }
    token.erase(std::remove_if(token.begin(), token.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }), token.end());
    CHECK((token.size() >= MIN_TOKEN_LENGTH) && (token.size() <= MAX_TOKEN_LENGTH), HAILO_INVALID_OPERATION,
        "Debug shell token is invalid");
    return token;
}

bool constant_time_equal(const std::string &lhs, const std::string &rhs)
{
    const auto comparison_length = std::max(lhs.size(), rhs.size());
    size_t difference = lhs.size() ^ rhs.size();
    for (size_t index = 0; index < comparison_length; ++index) {
        const auto lhs_value = (index < lhs.size()) ? static_cast<unsigned char>(lhs[index]) : 0;
        const auto rhs_value = (index < rhs.size()) ? static_cast<unsigned char>(rhs[index]) : 0;
        difference |= static_cast<size_t>(lhs_value ^ rhs_value);
    }
    return 0 == difference;
}

hailo_status authenticate_token(const std::string &token)
{
    CHECK((token.size() >= MIN_TOKEN_LENGTH) && (token.size() <= MAX_TOKEN_LENGTH), HAILO_INVALID_ARGUMENT,
        "Debug shell token length must be between {} and {} bytes", MIN_TOKEN_LENGTH, MAX_TOKEN_LENGTH);
    TRY(auto expected_token, read_expected_token());
    CHECK(constant_time_equal(expected_token, token), HAILO_INVALID_OPERATION, "Debug shell authentication failed");
    return HAILO_SUCCESS;
}

class InteractiveDebugShell final
{
public:
    static InteractiveDebugShell &instance()
    {
        static InteractiveDebugShell shell;
        return shell;
    }

    Expected<uint64_t> open(uint32_t rows, uint32_t columns)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        close_locked();

        const auto master_fd = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
        CHECK(master_fd >= 0, HAILO_INTERNAL_FAILURE, "Failed opening debug shell PTY");
        if ((0 != grantpt(master_fd)) || (0 != unlockpt(master_fd))) {
            (void)::close(master_fd);
            return make_unexpected(HAILO_INTERNAL_FAILURE);
        }

        std::array<char, 256> slave_path = {};
        if (0 != ptsname_r(master_fd, slave_path.data(), slave_path.size())) {
            (void)::close(master_fd);
            return make_unexpected(HAILO_INTERNAL_FAILURE);
        }

        const auto child_pid = fork();
        if (child_pid < 0) {
            (void)::close(master_fd);
            return make_unexpected(HAILO_INTERNAL_FAILURE);
        }
        if (0 == child_pid) {
            (void)setsid();
            const auto slave_fd = ::open(slave_path.data(), O_RDWR);
            if (slave_fd < 0) {
                _exit(126);
            }
            (void)ioctl(slave_fd, TIOCSCTTY, 0);
            apply_window_size(slave_fd, rows, columns);
            if ((0 > dup2(slave_fd, STDIN_FILENO)) || (0 > dup2(slave_fd, STDOUT_FILENO)) ||
                (0 > dup2(slave_fd, STDERR_FILENO))) {
                _exit(126);
            }
            if (slave_fd > STDERR_FILENO) {
                (void)::close(slave_fd);
            }
            (void)::close(master_fd);
            if ((0 != chdir("/")) || (0 != setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1)) ||
                (0 != setenv("TERM", "xterm-256color", 1))) {
                _exit(126);
            }
            execl("/bin/sh", "sh", "-i", static_cast<char *>(nullptr));
            _exit(127);
        }

        const auto flags = fcntl(master_fd, F_GETFL, 0);
        if ((flags < 0) || (0 != fcntl(master_fd, F_SETFL, flags | O_NONBLOCK))) {
            (void)kill(-child_pid, SIGKILL);
            (void)kill(child_pid, SIGKILL);
            (void)waitpid(child_pid, nullptr, 0);
            (void)::close(master_fd);
            return make_unexpected(HAILO_INTERNAL_FAILURE);
        }

        m_master_fd = master_fd;
        m_child_pid = child_pid;
        m_exit_code = -1;
        m_session_id = m_next_session_id++;
        apply_window_size(m_master_fd, rows, columns);
        return Expected<uint64_t>(m_session_id);
    }

    Expected<DebugShellIoSerializer::Reply> io(const DebugShellIoSerializer::Request &request)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        CHECK((0 != m_session_id) && (request.session_id == m_session_id), HAILO_NOT_FOUND,
            "Debug shell session does not exist");
        CHECK(request.input.size() <= MAX_INTERACTIVE_INPUT_BYTES, HAILO_INVALID_ARGUMENT,
            "Debug shell input exceeds {} bytes", MAX_INTERACTIVE_INPUT_BYTES);
        CHECK(request.timeout_ms <= MAX_INTERACTIVE_POLL_MS, HAILO_INVALID_ARGUMENT,
            "Debug shell poll timeout exceeds {} ms", MAX_INTERACTIVE_POLL_MS);
        CHECK((request.max_output_bytes > 0) && (request.max_output_bytes <= MAX_OUTPUT_BYTES),
            HAILO_INVALID_ARGUMENT, "Debug shell output limit must be between 1 and {} bytes", MAX_OUTPUT_BYTES);
        CHECK((request.rows <= MAX_TERMINAL_DIMENSION) && (request.columns <= MAX_TERMINAL_DIMENSION),
            HAILO_INVALID_ARGUMENT, "Debug shell terminal size is invalid");

        if (m_master_fd >= 0) {
            apply_window_size(m_master_fd, request.rows, request.columns);
            size_t input_offset = 0;
            while (input_offset < request.input.size()) {
                const auto bytes_written = write(m_master_fd, request.input.data() + input_offset,
                    request.input.size() - input_offset);
                if (bytes_written > 0) {
                    input_offset += static_cast<size_t>(bytes_written);
                } else if ((bytes_written < 0) && ((EAGAIN == errno) || (EWOULDBLOCK == errno))) {
                    pollfd writable = {m_master_fd, POLLOUT, 0};
                    (void)poll(&writable, 1, POLL_INTERVAL_MS);
                } else {
                    mark_eof_locked();
                    break;
                }
            }
        }

        std::string output;
        output.reserve(request.max_output_bytes);
        if (m_master_fd >= 0) {
            pollfd readable = {m_master_fd, POLLIN | POLLHUP, 0};
            (void)poll(&readable, 1, static_cast<int>(request.timeout_ms));
            if (0 != (readable.revents & (POLLIN | POLLHUP))) {
                std::array<char, 4096> buffer = {};
                while (output.size() < request.max_output_bytes) {
                    const auto read_size = std::min(buffer.size(),
                        static_cast<size_t>(request.max_output_bytes) - output.size());
                    const auto bytes_read = read(m_master_fd, buffer.data(), read_size);
                    if (bytes_read > 0) {
                        output.append(buffer.data(), static_cast<size_t>(bytes_read));
                    } else if ((bytes_read < 0) && ((EAGAIN == errno) || (EWOULDBLOCK == errno))) {
                        break;
                    } else {
                        mark_eof_locked();
                        break;
                    }
                }
            }
        }

        update_child_status_locked();
        const bool eof = (m_master_fd < 0) && (m_child_pid < 0);
        return DebugShellIoSerializer::Reply{std::move(output), eof, eof ? m_exit_code : -1};
    }

    hailo_status close(uint64_t session_id)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        CHECK((0 != m_session_id) && (session_id == m_session_id), HAILO_NOT_FOUND,
            "Debug shell session does not exist");
        close_locked();
        return HAILO_SUCCESS;
    }

    ~InteractiveDebugShell()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        close_locked();
    }

private:
    InteractiveDebugShell() = default;

    static void apply_window_size(int fd, uint32_t rows, uint32_t columns)
    {
        winsize size = {};
        size.ws_row = static_cast<unsigned short>((0 == rows) ? 24 : rows);
        size.ws_col = static_cast<unsigned short>((0 == columns) ? 80 : columns);
        (void)ioctl(fd, TIOCSWINSZ, &size);
    }

    void update_child_status_locked()
    {
        if (m_child_pid < 0) {
            return;
        }
        int status = 0;
        const auto wait_result = waitpid(m_child_pid, &status, WNOHANG);
        if (m_child_pid == wait_result) {
            if (WIFEXITED(status)) {
                m_exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                m_exit_code = 128 + WTERMSIG(status);
            }
            m_child_pid = -1;
        }
    }

    void mark_eof_locked()
    {
        if (m_master_fd >= 0) {
            (void)::close(m_master_fd);
            m_master_fd = -1;
        }
        update_child_status_locked();
    }

    void close_locked()
    {
        if (m_master_fd >= 0) {
            (void)::close(m_master_fd);
            m_master_fd = -1;
        }
        if (m_child_pid >= 0) {
            (void)kill(-m_child_pid, SIGHUP);
            (void)kill(m_child_pid, SIGHUP);
            for (size_t attempt = 0; attempt < 10; ++attempt) {
                int status = 0;
                const auto wait_result = waitpid(m_child_pid, &status, WNOHANG);
                if (m_child_pid == wait_result) {
                    m_child_pid = -1;
                    break;
                }
                (void)usleep(10000);
            }
            if (m_child_pid >= 0) {
                (void)kill(-m_child_pid, SIGKILL);
                (void)kill(m_child_pid, SIGKILL);
                (void)waitpid(m_child_pid, nullptr, 0);
                m_child_pid = -1;
            }
        }
        m_session_id = 0;
        m_exit_code = -1;
    }

    std::mutex m_mutex;
    int m_master_fd = -1;
    pid_t m_child_pid = -1;
    uint64_t m_session_id = 0;
    uint64_t m_next_session_id = 1;
    int32_t m_exit_code = -1;
};

} /* namespace */

Expected<DebugShellExecSerializer::Reply> DebugShellRunner::execute(
    const DebugShellExecSerializer::Request &request)
{
    CHECK(!request.command.empty() && (request.command.size() <= MAX_COMMAND_LENGTH), HAILO_INVALID_ARGUMENT,
        "Debug shell command length must be between 1 and {} bytes", MAX_COMMAND_LENGTH);
    CHECK((request.timeout_ms > 0) && (request.timeout_ms <= MAX_TIMEOUT_MS), HAILO_INVALID_ARGUMENT,
        "Debug shell timeout must be between 1 and {} ms", MAX_TIMEOUT_MS);
    CHECK((request.max_output_bytes > 0) && (request.max_output_bytes <= MAX_OUTPUT_BYTES), HAILO_INVALID_ARGUMENT,
        "Debug shell output limit must be between 1 and {} bytes", MAX_OUTPUT_BYTES);
    CHECK_SUCCESS(authenticate_token(request.token));

    int output_pipe[2] = {-1, -1};
    CHECK(0 == pipe(output_pipe), HAILO_INTERNAL_FAILURE, "Failed creating debug shell output pipe");

    const auto child_pid = fork();
    if (0 == child_pid) {
        (void)setpgid(0, 0);
        ::close(output_pipe[0]);
        if ((0 > dup2(output_pipe[1], STDOUT_FILENO)) || (0 > dup2(output_pipe[1], STDERR_FILENO))) {
            _exit(126);
        }
        ::close(output_pipe[1]);
        if ((0 != chdir("/")) || (0 != setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1))) {
            _exit(126);
        }
        execl("/bin/sh", "sh", "-c", request.command.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }

    if (child_pid < 0) {
        ::close(output_pipe[0]);
        ::close(output_pipe[1]);
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    ::close(output_pipe[1]);
    (void)setpgid(child_pid, child_pid);
    const auto original_flags = fcntl(output_pipe[0], F_GETFL, 0);
    (void)fcntl(output_pipe[0], F_SETFL, original_flags | O_NONBLOCK);

    std::string output;
    output.reserve(request.max_output_bytes);
    bool pipe_eof = false;
    bool child_exited = false;
    bool timed_out = false;
    bool truncated = false;
    bool kill_sent = false;
    int child_status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(request.timeout_ms);

    auto terminate_child = [&]() {
        if (!kill_sent && !child_exited) {
            (void)kill(-child_pid, SIGKILL);
            (void)kill(child_pid, SIGKILL);
            kill_sent = true;
        }
    };

    while (!pipe_eof || !child_exited) {
        pollfd descriptor = {output_pipe[0], POLLIN | POLLHUP, 0};
        (void)poll(&descriptor, 1, POLL_INTERVAL_MS);

        if (0 != (descriptor.revents & (POLLIN | POLLHUP))) {
            char buffer[4096];
            while (true) {
                const auto bytes_read = read(output_pipe[0], buffer, sizeof(buffer));
                if (bytes_read > 0) {
                    const auto remaining = static_cast<size_t>(request.max_output_bytes) - output.size();
                    const auto bytes_to_append = std::min(remaining, static_cast<size_t>(bytes_read));
                    output.append(buffer, bytes_to_append);
                    if (bytes_to_append < static_cast<size_t>(bytes_read)) {
                        truncated = true;
                        terminate_child();
                    }
                } else if (0 == bytes_read) {
                    pipe_eof = true;
                    break;
                } else if ((EAGAIN == errno) || (EWOULDBLOCK == errno)) {
                    break;
                } else {
                    pipe_eof = true;
                    break;
                }
            }
        }

        const auto wait_result = waitpid(child_pid, &child_status, WNOHANG);
        if (child_pid == wait_result) {
            child_exited = true;
        }

        if (!child_exited && (std::chrono::steady_clock::now() >= deadline)) {
            timed_out = true;
            terminate_child();
        }
    }

    ::close(output_pipe[0]);
    if (!child_exited) {
        (void)waitpid(child_pid, &child_status, 0);
    }

    int32_t exit_code = 127;
    if (timed_out) {
        exit_code = 124;
    } else if (truncated) {
        exit_code = 125;
    } else if (WIFEXITED(child_status)) {
        exit_code = WEXITSTATUS(child_status);
    } else if (WIFSIGNALED(child_status)) {
        exit_code = 128 + WTERMSIG(child_status);
    }

    return DebugShellExecSerializer::Reply{exit_code, std::move(output), timed_out, truncated};
}

Expected<uint64_t> DebugShellRunner::open(const DebugShellOpenSerializer::Request &request)
{
    CHECK_SUCCESS(authenticate_token(request.token));
    CHECK((request.rows <= MAX_TERMINAL_DIMENSION) && (request.columns <= MAX_TERMINAL_DIMENSION),
        HAILO_INVALID_ARGUMENT, "Debug shell terminal size is invalid");
    return InteractiveDebugShell::instance().open(request.rows, request.columns);
}

Expected<DebugShellIoSerializer::Reply> DebugShellRunner::io(const DebugShellIoSerializer::Request &request)
{
    CHECK_SUCCESS(authenticate_token(request.token));
    return InteractiveDebugShell::instance().io(request);
}

hailo_status DebugShellRunner::close(const DebugShellCloseSerializer::Request &request)
{
    CHECK_SUCCESS(authenticate_token(request.token));
    return InteractiveDebugShell::instance().close(request.session_id);
}

} /* namespace hailort */
