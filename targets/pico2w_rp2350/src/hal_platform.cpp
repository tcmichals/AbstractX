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
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
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
#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
        printf("[Pico 2 W Wi-Fi] Connecting to SSID '%s'...\n", WIFI_SSID);
        if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 15000) != 0) {
            printf("[Pico 2 W Wi-Fi] WARNING: Failed to connect to Wi-Fi within 15s timeout.\n");
        } else {
            printf("[Pico 2 W Wi-Fi] Successfully connected! IP: %s\n",
                   ip4addr_ntoa(netif_ip4_addr(netif_default)));
        }
#else
        printf("[Pico 2 W Wi-Fi] STA mode ready (WIFI_SSID/WIFI_PASSWORD not set at compile time).\n");
#endif
    } else {
        printf("[Pico 2 W Wi-Fi] ERROR: cyw43_arch_init failed.\n");
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

#ifdef PICO_ON_DEVICE
#include "lwip/udp.h"
#include "lwip/pbuf.h"

static struct udp_pcb *g_pico_udp_pcb = nullptr;
static ip_addr_t g_pico_dest_ip{};
static bool g_pico_udp_ready = false;
#endif

void platform_send_telemetry(const uint8_t* data, size_t len) noexcept {
#ifdef PICO_ON_DEVICE
    if (!g_pico_udp_ready) {
        g_pico_udp_pcb = udp_new();
        ip4addr_aton("255.255.255.255", &g_pico_dest_ip);
        g_pico_udp_ready = true;
    }
    if (g_pico_udp_pcb && data && len > 0) {
        struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
        if (p) {
            std::memcpy(p->payload, data, len);
            udp_sendto(g_pico_udp_pcb, p, &g_pico_dest_ip, 9870);
            pbuf_free(p);
        }
    }
#else
    (void)data;
    (void)len;
#endif
}

} // namespace abstractx::hal
