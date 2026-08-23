/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/

#include "debug_shell.hpp"

#include "common/utils.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace hailort
{

namespace
{

constexpr auto DEBUG_SHELL_TOKEN_PATH = "/etc/hailort-debug-shell.token";
constexpr size_t MIN_TOKEN_LENGTH = 32;
constexpr size_t MAX_COMMAND_LENGTH = 2048;
constexpr uint32_t MAX_TIMEOUT_MS = 30000;
constexpr uint32_t MAX_OUTPUT_BYTES = 64 * 1024;
constexpr int POLL_INTERVAL_MS = 50;

Expected<std::string> read_expected_token()
{
    std::ifstream stream(DEBUG_SHELL_TOKEN_PATH);
    CHECK(stream.good(), HAILO_INVALID_OPERATION, "Debug shell token file is unavailable");
    std::string token;
    std::getline(stream, token);
    token.erase(std::remove_if(token.begin(), token.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }), token.end());
    CHECK(token.size() >= MIN_TOKEN_LENGTH, HAILO_INVALID_OPERATION, "Debug shell token is invalid");
    return token;
}

bool constant_time_equal(const std::string &lhs, const std::string &rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (size_t index = 0; index < lhs.size(); ++index) {
        difference |= static_cast<unsigned char>(lhs[index] ^ rhs[index]);
    }
    return 0 == difference;
}

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

    TRY(auto expected_token, read_expected_token());
    CHECK(constant_time_equal(expected_token, request.token), HAILO_INVALID_OPERATION,
        "Debug shell authentication failed");

    int output_pipe[2] = {-1, -1};
    CHECK(0 == pipe(output_pipe), HAILO_INTERNAL_FAILURE, "Failed creating debug shell output pipe");

    const auto child_pid = fork();
    if (0 == child_pid) {
        (void)setpgid(0, 0);
        close(output_pipe[0]);
        if ((0 > dup2(output_pipe[1], STDOUT_FILENO)) || (0 > dup2(output_pipe[1], STDERR_FILENO))) {
            _exit(126);
        }
        close(output_pipe[1]);
        if ((0 != chdir("/")) || (0 != setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1))) {
            _exit(126);
        }
        execl("/bin/sh", "sh", "-c", request.command.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }

    if (child_pid < 0) {
        close(output_pipe[0]);
        close(output_pipe[1]);
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    close(output_pipe[1]);
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

    close(output_pipe[0]);
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

} /* namespace hailort */
