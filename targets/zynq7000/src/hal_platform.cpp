/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Zynq-7000 Platform Hooks & Lifecycle Functions
 *
 * @impl [SPEC-ZYNQ-06] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-06
 */

#include "abstractx/hal/platform.hpp"

#include <pthread.h>
#include <poll.h>
#include <sched.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

namespace abstractx::hal {

static int g_udp_sock = -1;
static struct sockaddr_in g_dest_addr{};

void platform_init() noexcept {
    g_udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_udp_sock >= 0) {
        int broadcast = 1;
        setsockopt(g_udp_sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
        std::memset(&g_dest_addr, 0, sizeof(g_dest_addr));
        g_dest_addr.sin_family = AF_INET;
        g_dest_addr.sin_port = htons(9870);
        g_dest_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    }
}

namespace {
struct ThreadTrampoline {
    void (*entry)();
};

extern "C" void* posix_worker_thunk(void* arg) {
    auto* tramp = static_cast<ThreadTrampoline*>(arg);
    if (tramp && tramp->entry) {
        tramp->entry();
    }
    return nullptr;
}
static ThreadTrampoline s_worker_tramp{};
} // namespace

void platform_launch_processing_domain(void (*entry)()) noexcept {
    if (entry) {
        s_worker_tramp.entry = entry;
        pthread_t th{};
        if (pthread_create(&th, nullptr, posix_worker_thunk, &s_worker_tramp) == 0) {
            pthread_detach(th);
        }
    }
}

void platform_idle_wait() noexcept {
    // Cooperative yield with sub-millisecond backoff using sched_yield
    sched_yield();
}

void platform_poll_network() noexcept {
    // Zynq Linux network polling
}

// Zynq Linux: static pool + sinks; the kernel copies on write()/sendto().
trace::ITraceTransport* platform_trace_transport() noexcept {
    return nullptr;
}

void platform_send_telemetry(const uint8_t* data, size_t len) noexcept {
    if (g_udp_sock >= 0 && data && len > 0) {
        sendto(g_udp_sock, data, len, 0,
               reinterpret_cast<struct sockaddr*>(&g_dest_addr), sizeof(g_dest_addr));
    }
}

} // namespace abstractx::hal
