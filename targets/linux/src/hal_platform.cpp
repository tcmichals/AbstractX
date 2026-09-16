/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Linux Platform Hooks & Lifecycle Functions
 */

#include "abstractx/hal/platform.hpp"
#include <thread>
#include <chrono>

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

void platform_launch_processing_domain(void (*entry)()) noexcept {
    if (entry) {
        std::thread worker_thread(entry);
        worker_thread.detach();
    }
}

void platform_idle_wait() noexcept {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
}

void platform_poll_network() noexcept {
    // Linux network polling
}

void platform_send_telemetry(const uint8_t* data, size_t len) noexcept {
    if (g_udp_sock >= 0 && data && len > 0) {
        sendto(g_udp_sock, data, len, 0,
               reinterpret_cast<struct sockaddr*>(&g_dest_addr), sizeof(g_dest_addr));
    }
}

} // namespace abstractx::hal
