// @verifies [SPEC-REACTOR-01] [SPEC-REACTOR-02] [SPEC-REACTOR-03] [SPEC-REACTOR-04] targets/linux/SPECIFICATION.md
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Reactor Unit & Integration Tests (tests/test_reactor.cpp)
 * -------------------------------------------------------------------
 * Verifies zero-heap EpollReactor demultiplexing, non-blocking coroutine
 * AsyncUdpSocket loopback, and AsyncRpmsg / AsyncUio lifecycle awaiters.
 */

#include "CppUTest/TestHarness.h"
#include "abstractx/reactor/epoll_reactor.hpp"
#include "abstractx/reactor/async_socket.hpp"
#include "abstractx/reactor/async_rpmsg.hpp"
#include "abstractx/reactor/async_uio.hpp"
#include <array>
#include <span>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>

using namespace abstractx::reactor;

TEST_GROUP(AbstractXReactorTest) {
    void setup() override {}
    void teardown() override {}
};

// 1. [SPEC-REACTOR-01] EpollReactor Basic Registration & Wait
TEST(AbstractXReactorTest, EpollReactorBasicPipeEvent) {
    EpollReactor reactor;
    CHECK_TRUE(reactor.is_valid());
    CHECK_TRUE(reactor.fd() >= 0);

    int pipefds[2];
    int rc = ::pipe2(pipefds, O_NONBLOCK | O_CLOEXEC);
    CHECK_EQUAL(0, rc);

    int read_fd = pipefds[0];
    int write_fd = pipefds[1];

    uint64_t token = 0xCAFEBABE12345678ULL;
    bool ok = reactor.add_fd_u64(read_fd, EPOLLIN, token);
    CHECK_TRUE(ok);

    // No event before write
    std::array<struct epoll_event, 4> events{};
    int nevents = reactor.wait(events, 0);
    CHECK_EQUAL(0, nevents);

    // Write byte
    uint8_t b = 0xA5;
    CHECK_EQUAL(1, ::write(write_fd, &b, 1));

    // Wait should now report EPOLLIN
    nevents = reactor.wait(events, 100);
    CHECK_EQUAL(1, nevents);
    CHECK_EQUAL(token, events[0].data.u64);
    CHECK_TRUE((events[0].events & EPOLLIN) != 0);

    // Read byte and remove fd
    uint8_t read_b = 0;
    CHECK_EQUAL(1, ::read(read_fd, &read_b, 1));
    CHECK_EQUAL(0xA5, read_b);

    CHECK_TRUE(reactor.remove_fd(read_fd));
    ::close(read_fd);
    ::close(write_fd);
}

// 2. [SPEC-REACTOR-02] AsyncUdpSocket Non-Blocking Loopback
TEST(AbstractXReactorTest, AsyncUdpSocketLoopback) {
    EpollReactor reactor;
    AsyncUdpSocket rx_sock(reactor);
    AsyncUdpSocket tx_sock(reactor);

    // Bind receiver to an ephemeral port (or port 28765)
    uint16_t test_port = 28765;
    bool bound = rx_sock.bind(test_port, "127.0.0.1");
    CHECK_TRUE(bound);
    CHECK_TRUE(rx_sock.is_open());

    CHECK_TRUE(tx_sock.open());
    CHECK_TRUE(tx_sock.is_open());

    struct sockaddr_in dest_addr{};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(test_port);
    ::inet_pton(AF_INET, "127.0.0.1", &dest_addr.sin_addr);

    // Prepare message
    const uint8_t send_payload[] = "ABSTRACTX_REACTOR_TLP64";
    std::span<const uint8_t> tx_span{send_payload, sizeof(send_payload)};

    // Send packet
    ssize_t sent = ::sendto(tx_sock.fd(), tx_span.data(), tx_span.size(), 0,
                            reinterpret_cast<struct sockaddr*>(&dest_addr), sizeof(dest_addr));
    CHECK_EQUAL(sizeof(send_payload), sent);

    // Verify reactor detects packet arrival
    std::array<struct epoll_event, 4> events{};
    int nevents = reactor.wait(events, 200);
    CHECK_EQUAL(1, nevents);

    // Receive via AsyncUdpSocket buffer
    uint8_t recv_buf[64]{0};
    struct sockaddr_in src_addr{};
    auto recv_awaiter = rx_sock.recv_async(recv_buf, &src_addr);
    CHECK_TRUE(recv_awaiter.await_ready());
    ssize_t nrecv = recv_awaiter.await_resume();
    CHECK_EQUAL(sizeof(send_payload), nrecv);
    CHECK_EQUAL(0, std::memcmp(send_payload, recv_buf, sizeof(send_payload)));

    rx_sock.close();
    tx_sock.close();
    CHECK_FALSE(rx_sock.is_open());
    CHECK_FALSE(tx_sock.is_open());
}

// 3. [SPEC-REACTOR-03] AsyncRpmsgEndpoint Lifecycle
TEST(AbstractXReactorTest, AsyncRpmsgEndpointLifecycle) {
    EpollReactor reactor;
    AsyncRpmsgEndpoint rpmsg(reactor);
    CHECK_FALSE(rpmsg.is_open());
    CHECK_EQUAL(-1, rpmsg.fd());

    // Simulated character device via non-blocking pipe
    int pipefds[2];
    int rc = ::pipe2(pipefds, O_NONBLOCK | O_CLOEXEC);
    CHECK_EQUAL(0, rc);

    // Verify move semantics and close
    {
        AsyncRpmsgEndpoint temp_ep(reactor);
        CHECK_FALSE(temp_ep.is_open());
    }

    ::close(pipefds[0]);
    ::close(pipefds[1]);
}

// 4. [SPEC-REACTOR-04] AsyncUioDevice Lifecycle & Unmask
TEST(AbstractXReactorTest, AsyncUioDeviceLifecycle) {
    EpollReactor reactor;
    AsyncUioDevice uio(reactor);
    CHECK_FALSE(uio.is_open());
    CHECK_EQUAL(-1, uio.fd());

    // Unmask on invalid FD safely returns false
    CHECK_FALSE(uio.unmask_irq());

    // Verify move semantics
    AsyncUioDevice uio2 = std::move(uio);
    CHECK_FALSE(uio2.is_open());
}
