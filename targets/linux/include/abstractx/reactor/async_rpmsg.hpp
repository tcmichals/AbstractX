// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
// @impl [SPEC-REACTOR-03] targets/linux/SPECIFICATION.md#spec-reactor-03

#ifndef ABSTRACTX_REACTOR_ASYNC_RPMSG_HPP
#define ABSTRACTX_REACTOR_ASYNC_RPMSG_HPP

#include <fcntl.h>
#include <unistd.h>
#include <cstdint>
#include <span>
#include <coroutine>
#include "asp_coro.hpp"
#include "epoll_reactor.hpp"

namespace abstractx::reactor {

/**
 * @brief Zero-Copy Asynchronous Linux RPMsg-Lite Character Endpoint
 * 
 * Interacts with Linux virtio_rpmsg_bus character devices (/dev/rpmsgX)
 * providing non-blocking C++20 coroutine awaitables for inter-core communication
 * (e.g. Linux ARM <-> XuanTie E906 RISC-V / Cortex-M33).
 */
class AsyncRpmsgEndpoint {
public:
    explicit AsyncRpmsgEndpoint(EpollReactor& reactor) noexcept
        : reactor_(reactor), fd_(-1) {}

    ~AsyncRpmsgEndpoint() noexcept {
        close();
    }

    AsyncRpmsgEndpoint(const AsyncRpmsgEndpoint&) = delete;
    AsyncRpmsgEndpoint& operator=(const AsyncRpmsgEndpoint&) = delete;

    AsyncRpmsgEndpoint(AsyncRpmsgEndpoint&& other) noexcept
        : reactor_(other.reactor_), fd_(other.fd_) {
        other.fd_ = -1;
    }

    AsyncRpmsgEndpoint& operator=(AsyncRpmsgEndpoint&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    bool open(const char* dev_path = "/dev/rpmsg0") noexcept {
        if (fd_ >= 0) return true;
        fd_ = ::open(dev_path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
        return fd_ >= 0;
    }

    void close() noexcept {
        if (fd_ >= 0) {
            reactor_.remove_fd(fd_);
            ::close(fd_);
            fd_ = -1;
        }
    }

    [[nodiscard]] int fd() const noexcept { return fd_; }
    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

    /**
     * @brief Asynchronous Recv Awaiter for RPMsg Datagrams
     */
    struct RecvAwaiter {
        AsyncRpmsgEndpoint& endpoint;
        std::span<uint8_t> buffer;
        ssize_t result{0};

        bool await_ready() noexcept {
            result = ::read(endpoint.fd_, buffer.data(), buffer.size());
            return (result >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK));
        }

        bool await_suspend(std::coroutine_handle<> handle) noexcept {
            return endpoint.reactor_.modify_fd(endpoint.fd_, EPOLLIN | EPOLLONESHOT, handle.address()) ||
                   endpoint.reactor_.add_fd(endpoint.fd_, EPOLLIN | EPOLLONESHOT, handle.address());
        }

        ssize_t await_resume() noexcept {
            if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                result = ::read(endpoint.fd_, buffer.data(), buffer.size());
            }
            return result;
        }
    };

    /**
     * @brief Asynchronous Send Awaiter for RPMsg Datagrams
     */
    struct SendAwaiter {
        AsyncRpmsgEndpoint& endpoint;
        std::span<const uint8_t> buffer;
        ssize_t result{0};

        bool await_ready() noexcept {
            result = ::write(endpoint.fd_, buffer.data(), buffer.size());
            return (result >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK));
        }

        bool await_suspend(std::coroutine_handle<> handle) noexcept {
            return endpoint.reactor_.modify_fd(endpoint.fd_, EPOLLOUT | EPOLLONESHOT, handle.address()) ||
                   endpoint.reactor_.add_fd(endpoint.fd_, EPOLLOUT | EPOLLONESHOT, handle.address());
        }

        ssize_t await_resume() noexcept {
            if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                result = ::write(endpoint.fd_, buffer.data(), buffer.size());
            }
            return result;
        }
    };

    RecvAwaiter recv_async(std::span<uint8_t> buf) noexcept {
        return RecvAwaiter{*this, buf};
    }

    SendAwaiter send_async(std::span<const uint8_t> buf) noexcept {
        return SendAwaiter{*this, buf};
    }

private:
    EpollReactor& reactor_;
    int fd_{-1};
};

} // namespace abstractx::reactor

#endif // ABSTRACTX_REACTOR_ASYNC_RPMSG_HPP
