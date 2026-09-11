/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Target: Lightweight EventFd Wrapper
 */

#ifndef ABSTRACTX_TARGET_EVENTFD_HPP
#define ABSTRACTX_TARGET_EVENTFD_HPP

#include <sys/eventfd.h>
#include <unistd.h>
#include <cstdint>

namespace abstractx::target {

class EventFd {
public:
    explicit EventFd(bool nonblocking = true) noexcept {
        int flags = EFD_CLOEXEC;
        if (nonblocking) flags |= EFD_NONBLOCK;
        fd_ = ::eventfd(0, flags);
    }

    ~EventFd() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    EventFd(const EventFd&) = delete;
    EventFd& operator=(const EventFd&) = delete;

    EventFd(EventFd&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    EventFd& operator=(EventFd&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) ::close(fd_);
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    int fd() const noexcept { return fd_; }
    bool is_valid() const noexcept { return fd_ >= 0; }

    bool notify(uint64_t counter = 1) noexcept {
        if (fd_ < 0) return false;
        return (::write(fd_, &counter, sizeof(counter)) == sizeof(counter));
    }

    uint64_t drain() noexcept {
        if (fd_ < 0) return 0;
        uint64_t val = 0;
        ssize_t ret = ::read(fd_, &val, sizeof(val));
        return (ret == sizeof(val)) ? val : 0;
    }

    uint64_t wait_blocking() noexcept {
        if (fd_ < 0) return 0;
        uint64_t val = 0;
        ssize_t ret = ::read(fd_, &val, sizeof(val));
        return (ret == sizeof(val)) ? val : 0;
    }

private:
    int fd_{-1};
};

} // namespace abstractx::target

#endif // ABSTRACTX_TARGET_EVENTFD_HPP
