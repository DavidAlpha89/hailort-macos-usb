/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file os_utils.cpp
 * @brief Utilities for Posix methods
 **/

#include "hailo/hailort.h"
#include "common/os_utils.hpp"
#include "common/utils.hpp"
#include "spdlog/sinks/syslog_sink.h"

#include <unistd.h>
#include <signal.h>
#include <sched.h>
#if !defined(__APPLE__)
#include <sys/syscall.h>
#endif

#if defined(__QNX__)
#define OS_UTILS__QNX_PAGE_SIZE (4096)
#endif /* defined(__QNX__) */
namespace hailort
{

#define EXISTENCE_CHECK_SIGNAL (0)

HailoRTOSLogger::HailoRTOSLogger()
{
    m_hailort_os_logger = spdlog::syslog_logger_mt("syslog", "hailort_service", LOG_PID);
    m_hailort_os_logger->set_pattern("%v");
    m_hailort_os_logger->set_level(spdlog::level::debug);
}

uint32_t OsUtils::get_curr_pid()
{
    return getpid();
}

uint32_t OsUtils::get_curr_tid()
{
#if defined(__APPLE__)
    uint64_t tid = 0;
    const auto status = pthread_threadid_np(nullptr, &tid);
    return (0 == status) ? static_cast<uint32_t>(tid) : 0;
#else
    return static_cast<uint32_t>(syscall(SYS_gettid));
#endif
}

bool OsUtils::is_pid_alive(uint32_t pid)
{
    return (0 == kill(pid, EXISTENCE_CHECK_SIGNAL));
}

void OsUtils::set_current_thread_name(const std::string &name)
{
    (void)name;
#ifndef NDEBUG
    // pthread_setname_np name size is limited to 16 chars (including null terminator)
    assert(name.size() < 16);
#if defined(__APPLE__)
    pthread_setname_np(name.c_str());
#else
    pthread_setname_np(pthread_self(), name.c_str());
#endif
#endif /* NDEBUG */
}

hailo_status OsUtils::set_current_thread_affinity(uint8_t cpu_index)
{
#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_index, &cpuset);

    static const pid_t CURRENT_THREAD = 0;
    int rc = sched_setaffinity(CURRENT_THREAD, sizeof(cpu_set_t), &cpuset);
    CHECK(rc == 0, HAILO_INTERNAL_FAILURE, "sched_setaffinity failed with status {}", rc);

    return HAILO_SUCCESS;
#elif defined(__QNX__) || defined(__APPLE__)
    (void)cpu_index;
    // TODO: impl on qnx (HRT-10889)
    return HAILO_NOT_IMPLEMENTED;
#endif
}

size_t OsUtils::get_page_size()
{
#if defined(__linux__)
    static const auto page_size = sysconf(_SC_PAGESIZE);
    return static_cast<size_t>(page_size);
#elif defined(__QNX__)
    return OS_UTILS__QNX_PAGE_SIZE;
#elif defined(__APPLE__)
    static const auto page_size = sysconf(_SC_PAGESIZE);
    return static_cast<size_t>(page_size);
#endif
}

int OsUtils::set_environment_variable(const std::string &name, const std::string &value)
{
    return setenv(name.c_str(), value.c_str(), true);
}

CursorAdjustment::CursorAdjustment(){}
CursorAdjustment::~CursorAdjustment(){}

} /* namespace hailort */
