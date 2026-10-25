// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
// @impl [SPEC-REACTOR-04] targets/linux/SPECIFICATION.md#spec-reactor-04

#ifndef ABSTRACTX_REACTOR_ASYNC_UIO_HPP
#define ABSTRACTX_REACTOR_ASYNC_UIO_HPP

#include <fcntl.h>
#include <unistd.h>
#include <cstdint>
#include <span>
#include <coroutine>
#include "asp_coro.hpp"
#include "epoll_reactor.hpp"

namespace abstractx::reactor {

/**
 * @brief Asynchronous Linux UIO Interrupt Awaiter for AbstractX Reactor
 * 
 * Manages physical FPGA crossbar interrupts exposed via /dev/uioX.
 * Coroutines suspend until the hardware IRQ fires with zero CPU busy-spinning.
 */
class AsyncUioDevice {
public:
    explicit AsyncUioDevice(EpollReactor& reactor) noexcept
        : reactor_(reactor), fd_(-1) {}

    ~AsyncUioDevice() noexcept {
        close();
    }

    AsyncUioDevice(const AsyncUioDevice&) = delete;
    AsyncUioDevice& operator=(const AsyncUioDevice&) = delete;

    AsyncUioDevice(AsyncUioDevice&& other) noexcept
        : reactor_(other.reactor_), fd_(other.fd_) {
        other.fd_ = -1;
    }

    AsyncUioDevice& operator=(AsyncUioDevice&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    bool open(const char* dev_path = "/dev/uio0") noexcept {
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

    bool unmask_irq() noexcept {
        if (fd_ < 0) return false;
        uint32_t unmask = 1;
        return (::write(fd_, &unmask, sizeof(unmask)) == sizeof(unmask));
    }

    [[nodiscard]] int fd() const noexcept { return fd_; }
    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

    /**
     * @brief Asynchronous Awaiter for UIO Hardware Interrupts
     */
    struct IrqAwaiter {
        AsyncUioDevice& uio;
        uint32_t irq_count{0};
        bool ready{false};

        bool await_ready() noexcept {
            // Unmask IRQ before sleeping
            uio.unmask_irq();
            ssize_t n = ::read(uio.fd_, &irq_count, sizeof(irq_count));
            ready = (n == sizeof(irq_count));
            return ready;
        }

        bool await_suspend(std::coroutine_handle<> handle) noexcept {
            return uio.reactor_.modify_fd(uio.fd_, EPOLLIN | EPOLLONESHOT, handle.address()) ||
                   uio.reactor_.add_fd(uio.fd_, EPOLLIN | EPOLLONESHOT, handle.address());
        }

        uint32_t await_resume() noexcept {
            if (!ready) {
                ::read(uio.fd_, &irq_count, sizeof(irq_count));
            }
            return irq_count;
        }
    };

    IrqAwaiter await_irq_async() noexcept {
        return IrqAwaiter{*this};
    }

private:
    EpollReactor& reactor_;
    int fd_{-1};
};

} // namespace abstractx::reactor

#endif // ABSTRACTX_REACTOR_ASYNC_UIO_HPP
