// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
// @impl [SPEC-REACTOR-01] targets/linux/SPECIFICATION.md#spec-reactor-01

#ifndef ABSTRACTX_REACTOR_EPOLL_REACTOR_HPP
#define ABSTRACTX_REACTOR_EPOLL_REACTOR_HPP

#include <sys/epoll.h>
#include <unistd.h>
#include <cstdint>
#include <span>
#include <coroutine>

namespace abstractx::reactor {

/**
 * @brief Zero-Heap C++20 Linux Epoll Demultiplexer Engine
 * 
 * Manages Linux epoll file descriptor subscriptions with zero dynamic memory allocation.
 * Passes events via caller-provided std::span<struct epoll_event>.
 */
class EpollReactor {
public:
    explicit EpollReactor() noexcept {
        epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    }

    ~EpollReactor() noexcept {
        if (epoll_fd_ >= 0) {
            ::close(epoll_fd_);
            epoll_fd_ = -1;
        }
    }

    EpollReactor(const EpollReactor&) = delete;
    EpollReactor& operator=(const EpollReactor&) = delete;

    EpollReactor(EpollReactor&& other) noexcept : epoll_fd_(other.epoll_fd_) {
        other.epoll_fd_ = -1;
    }

    EpollReactor& operator=(EpollReactor&& other) noexcept {
        if (this != &other) {
            if (epoll_fd_ >= 0) ::close(epoll_fd_);
            epoll_fd_ = other.epoll_fd_;
            other.epoll_fd_ = -1;
        }
        return *this;
    }

    [[nodiscard]] int fd() const noexcept { return epoll_fd_; }
    [[nodiscard]] bool is_valid() const noexcept { return epoll_fd_ >= 0; }

    bool add_fd(int fd, uint32_t events, void* user_data = nullptr) noexcept {
        if (epoll_fd_ < 0 || fd < 0) return false;
        struct epoll_event ev{};
        ev.events = events;
        ev.data.ptr = user_data;
        return (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == 0);
    }

    bool add_fd_u64(int fd, uint32_t events, uint64_t u64_data) noexcept {
        if (epoll_fd_ < 0 || fd < 0) return false;
        struct epoll_event ev{};
        ev.events = events;
        ev.data.u64 = u64_data;
        return (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == 0);
    }

    bool modify_fd(int fd, uint32_t events, void* user_data = nullptr) noexcept {
        if (epoll_fd_ < 0 || fd < 0) return false;
        struct epoll_event ev{};
        ev.events = events;
        ev.data.ptr = user_data;
        return (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == 0);
    }

    bool remove_fd(int fd) noexcept {
        if (epoll_fd_ < 0 || fd < 0) return false;
        return (::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == 0);
    }

    int wait(std::span<struct epoll_event> events, int timeout_ms = -1) noexcept {
        if (epoll_fd_ < 0 || events.empty()) return -1;
        return ::epoll_wait(epoll_fd_, events.data(), static_cast<int>(events.size()), timeout_ms);
    }

private:
    int epoll_fd_{-1};
};

} // namespace abstractx::reactor

// Backward compatibility alias for targets/linux/include/epoll_reactor.hpp
namespace abstractx::target {
    using EpollReactor = ::abstractx::reactor::EpollReactor;
}

#endif // ABSTRACTX_REACTOR_EPOLL_REACTOR_HPP
