// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
// @impl [SPEC-REACTOR-02] targets/linux/SPECIFICATION.md#spec-reactor-02

#ifndef ABSTRACTX_REACTOR_ASYNC_SOCKET_HPP
#define ABSTRACTX_REACTOR_ASYNC_SOCKET_HPP

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdint>
#include <span>
#include <coroutine>
#include "asp_coro.hpp"
#include "epoll_reactor.hpp"

namespace abstractx::reactor {

/**
 * @brief Zero-Heap Asynchronous UDP Socket for AbstractX Reactor
 * 
 * Provides non-blocking datagram sending and receiving via C++20 coroutines.
 * Operates directly on user-provided std::span buffers with zero heap allocation.
 */
class AsyncUdpSocket {
public:
    explicit AsyncUdpSocket(EpollReactor& reactor) noexcept
        : reactor_(reactor), fd_(-1) {}

    ~AsyncUdpSocket() noexcept {
        close();
    }

    AsyncUdpSocket(const AsyncUdpSocket&) = delete;
    AsyncUdpSocket& operator=(const AsyncUdpSocket&) = delete;

    AsyncUdpSocket(AsyncUdpSocket&& other) noexcept
        : reactor_(other.reactor_), fd_(other.fd_) {
        other.fd_ = -1;
    }

    AsyncUdpSocket& operator=(AsyncUdpSocket&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    bool open() noexcept {
        if (fd_ >= 0) return true;
        fd_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        return fd_ >= 0;
    }

    bool bind(uint16_t port, const char* ip = nullptr) noexcept {
        if (!open()) return false;

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (ip != nullptr && ::inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
            return false;
        } else if (ip == nullptr) {
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
        }

        int reuse = 1;
        ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        if (::bind(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
            return false;
        }

        return reactor_.add_fd(fd_, EPOLLIN, this);
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
     * @brief Asynchronous Recv Awaiter
     */
    struct RecvAwaiter {
        AsyncUdpSocket& socket;
        std::span<uint8_t> buffer;
        struct sockaddr_in* src_addr;
        ssize_t result{0};

        bool await_ready() noexcept {
            socklen_t addr_len = sizeof(struct sockaddr_in);
            result = ::recvfrom(socket.fd_, buffer.data(), buffer.size(), 0,
                                src_addr ? reinterpret_cast<struct sockaddr*>(src_addr) : nullptr,
                                src_addr ? &addr_len : nullptr);
            return (result >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK));
        }

        bool await_suspend(std::coroutine_handle<> handle) noexcept {
            return socket.reactor_.modify_fd(socket.fd_, EPOLLIN | EPOLLONESHOT, handle.address()) ||
                   socket.reactor_.add_fd(socket.fd_, EPOLLIN | EPOLLONESHOT, handle.address());
        }

        ssize_t await_resume() noexcept {
            if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                socklen_t addr_len = sizeof(struct sockaddr_in);
                result = ::recvfrom(socket.fd_, buffer.data(), buffer.size(), 0,
                                    src_addr ? reinterpret_cast<struct sockaddr*>(src_addr) : nullptr,
                                    src_addr ? &addr_len : nullptr);
            }
            return result;
        }
    };

    /**
     * @brief Asynchronous Send Awaiter
     */
    struct SendAwaiter {
        AsyncUdpSocket& socket;
        std::span<const uint8_t> buffer;
        const struct sockaddr_in& dest_addr;
        ssize_t result{0};

        bool await_ready() noexcept {
            result = ::sendto(socket.fd_, buffer.data(), buffer.size(), 0,
                              reinterpret_cast<const struct sockaddr*>(&dest_addr), sizeof(dest_addr));
            return (result >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK));
        }

        bool await_suspend(std::coroutine_handle<> handle) noexcept {
            return socket.reactor_.modify_fd(socket.fd_, EPOLLOUT | EPOLLONESHOT, handle.address()) ||
                   socket.reactor_.add_fd(socket.fd_, EPOLLOUT | EPOLLONESHOT, handle.address());
        }

        ssize_t await_resume() noexcept {
            if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                result = ::sendto(socket.fd_, buffer.data(), buffer.size(), 0,
                                  reinterpret_cast<const struct sockaddr*>(&dest_addr), sizeof(dest_addr));
            }
            return result;
        }
    };

    RecvAwaiter recv_async(std::span<uint8_t> buf, struct sockaddr_in* src = nullptr) noexcept {
        return RecvAwaiter{*this, buf, src};
    }

    SendAwaiter send_to_async(std::span<const uint8_t> buf, const struct sockaddr_in& dest) noexcept {
        return SendAwaiter{*this, buf, dest};
    }

private:
    EpollReactor& reactor_;
    int fd_{-1};
};

} // namespace abstractx::reactor

#endif // ABSTRACTX_REACTOR_ASYNC_SOCKET_HPP
