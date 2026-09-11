/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Linux Platform Hooks & Lifecycle Functions
 */

#include "abstractx/hal/platform.hpp"
#include <thread>
#include <chrono>

namespace abstractx::hal {

void platform_init() noexcept {
    // Linux platform initialization
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
    // Linux network polling (e.g. epoll / socket drain)
}

} // namespace abstractx::hal
