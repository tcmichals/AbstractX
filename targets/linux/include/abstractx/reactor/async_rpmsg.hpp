// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
// @impl [SPEC-REACTOR-03] targets/linux/SPECIFICATION.md#spec-reactor-03

#ifndef ABSTRACTX_REACTOR_ASYNC_RPMSG_HPP
#define ABSTRACTX_REACTOR_ASYNC_RPMSG_HPP

#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <type_traits>
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
        : reactor_(&reactor), fd_(-1) {}

    ~AsyncRpmsgEndpoint() noexcept {
        close();
    }

    AsyncRpmsgEndpoint(const AsyncRpmsgEndpoint&) = delete;
    AsyncRpmsgEndpoint& operator=(const AsyncRpmsgEndpoint&) = delete;

    // Moving carries the reactor the fd is registered with. Do not move or close
    // while an awaiter is suspended: its coroutine would never be resumed.
    AsyncRpmsgEndpoint(AsyncRpmsgEndpoint&& other) noexcept
        : reactor_(other.reactor_), fd_(other.fd_) {
        other.fd_ = -1;
    }

    AsyncRpmsgEndpoint& operator=(AsyncRpmsgEndpoint&& other) noexcept {
        if (this != &other) {
            close();
            reactor_ = other.reactor_;
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
            reactor_->remove_fd(fd_);
            ::close(fd_);
            fd_ = -1;
        }
    }

    [[nodiscard]] int fd() const noexcept { return fd_; }
    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

    // One registration per fd (EPOLLONESHOT carries one handle): a second
    // concurrent awaiter would overwrite the first, so it fails with EBUSY.
    // errno is saved in the awaiter: by resume time other code has run.
    // @impl [SPEC-REACTOR-03] targets/linux/SPECIFICATION.md#spec-reactor-03
    template <bool kWrite>
    struct IoAwaiter {
        AsyncRpmsgEndpoint& endpoint;
        std::span<std::conditional_t<kWrite, const uint8_t, uint8_t>> buffer;
        ssize_t result{0};
        int err{0};
        bool registered{false};

        void attempt() noexcept {
            if constexpr (kWrite) {
                result = ::write(endpoint.fd_, buffer.data(), buffer.size());
            } else {
                result = ::read(endpoint.fd_, buffer.data(), buffer.size());
            }
            err = result < 0 ? errno : 0;
        }
        [[nodiscard]] bool would_block() const noexcept {
            return result < 0 && (err == EAGAIN || err == EWOULDBLOCK);
        }

        bool await_ready() noexcept {
            attempt();
            return !would_block();
        }

        bool await_suspend(std::coroutine_handle<> handle) noexcept {
            if (endpoint.waiting_) {
                result = -1;
                err = EBUSY;
                return false;
            }
            const uint32_t events = (kWrite ? EPOLLOUT : EPOLLIN) | EPOLLONESHOT;
            if (!endpoint.reactor_->modify_fd(endpoint.fd_, events, handle.address()) &&
                !endpoint.reactor_->add_fd(endpoint.fd_, events, handle.address())) {
                result = -1;
                err = errno;
                return false;
            }
            endpoint.waiting_ = registered = true;
            return true;
        }

        // Returns bytes moved, or -1 with the saved errno restored (EAGAIN after a
        // spurious wakeup: the caller awaits again).
        ssize_t await_resume() noexcept {
            if (registered) {
                endpoint.waiting_ = registered = false;
                if (would_block()) {
                    attempt();
                }
            }
            if (result < 0) {
                errno = err;
            }
            return result;
        }
    };
    using RecvAwaiter = IoAwaiter<false>;
    using SendAwaiter = IoAwaiter<true>;

    RecvAwaiter recv_async(std::span<uint8_t> buf) noexcept {
        return RecvAwaiter{*this, buf};
    }

    SendAwaiter send_async(std::span<const uint8_t> buf) noexcept {
        return SendAwaiter{*this, buf};
    }

private:
    EpollReactor* reactor_;
    int fd_{-1};
    bool waiting_{false};
};

} // namespace abstractx::reactor

#endif // ABSTRACTX_REACTOR_ASYNC_RPMSG_HPP
