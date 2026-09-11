/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Raspberry Pi Pico 2 W (RP2350) Platform Hooks & Factories
 * --------------------------------------------------------------------
 * Encapsulates RP2350 and CYW43439 hardware lifecycle, power states,
 * multicore launch, and universal HAL driver instances.
 */

#include "abstractx/hal/platform.hpp"
#include "abstractx_pico.hpp"

#ifdef PICO_ON_DEVICE
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/cyw43_arch.h"
#include "hardware/sync.h"
#endif

namespace abstractx::hal {

static PicoSpi   g_pico_spi;
static PicoUart  g_pico_uart;
static PicoTimer g_pico_timer;

ISpi& get_spi_driver() noexcept {
    return g_pico_spi;
}

IUart& get_uart_driver() noexcept {
    return g_pico_uart;
}

ITimer& get_timer_driver() noexcept {
    return g_pico_timer;
}

void platform_init() noexcept {
#ifdef PICO_ON_DEVICE
    stdio_init_all();
    if (cyw43_arch_init() == 0) {
        cyw43_arch_enable_sta_mode();
    }
#endif
}

void platform_launch_processing_domain(void (*entry)()) noexcept {
#ifdef PICO_ON_DEVICE
    multicore_launch_core1(entry);
#else
    if (entry) {
        entry();
    }
#endif
}

void platform_idle_wait() noexcept {
#ifdef PICO_ON_DEVICE
    __asm__ volatile("wfe");
#endif
}

void platform_poll_network() noexcept {
#ifdef PICO_ON_DEVICE
    cyw43_arch_poll();
#endif
}

} // namespace abstractx::hal
