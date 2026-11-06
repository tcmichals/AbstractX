/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Raspberry Pi Pico 2 W (RP2350) Platform Hooks & Factories
 * --------------------------------------------------------------------
 * Core 1 runs AbstractX; Core 0 runs only Wi-Fi ([SPEC-PICO-CORE-01]).
 */

#include "abstractx/hal/platform.hpp"
#include "abstractx/wake.hpp"
#include "abstractx/trace/tracer.hpp"
#include "abstractx_pico.hpp"
#include "hal/frame_ring.hpp"
#include "hal/wifi_join.hpp"

#ifdef PICO_ON_DEVICE
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/cyw43_arch.h"
#include "hardware/sync.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#endif

namespace abstractx::hal {

// @impl [SPEC-ARCH-16] docs/DESIGN_SPECIFICATION.md (debug check of post() / post_from_isr())
int in_isr() noexcept {
#ifdef PICO_ON_DEVICE
    uint32_t ipsr = 0;
    __asm volatile("mrs %0, ipsr" : "=r"(ipsr));  // Cortex-M: active exception number, 0 = thread
    return (ipsr & 0x1FFu) != 0u ? 1 : 0;
#else
    return -1;
#endif
}

static PicoSpi   g_pico_spi;
static PicoUart  g_pico_uart;
static PicoTimer g_pico_timer;

ISpi& get_spi_driver() noexcept {
    return g_pico_spi;
}

// @impl [SPEC-PICO-HAL-02] targets/pico2w_rp2350/SPECIFICATION.md
PicoSpi& get_pico_spi() noexcept {
    return g_pico_spi;
}

IUart& get_uart_driver() noexcept {
    return g_pico_uart;
}

ITimer& get_timer_driver() noexcept {
    return g_pico_timer;
}

// Port list ([SPEC-PICO-HAL-04]): SPI 0/1 -> SPI1, UART 0 -> UART0 (GPS), I2C 0 -> I2C0; else null.
// @impl [SPEC-PICO-HAL-04] targets/pico2w_rp2350/SPECIFICATION.md
ISpi* get_spi_port(uint8_t index) noexcept { return (index == 0 || index == 1) ? &g_pico_spi : nullptr; }
IUart* get_uart_port(uint8_t index) noexcept { return index == 0 ? &g_pico_uart : nullptr; }
II2c* get_i2c_port(uint8_t index) noexcept { return index == 0 ? &get_i2c_driver() : nullptr; }

// ---------------------------------------------------------------------------
// Core 1 -> Core 0 frame rings ([SPEC-PICO-CORE-01])
// ---------------------------------------------------------------------------
// @impl [SPEC-PICO-CORE-01] targets/pico2w_rp2350/SPECIFICATION.md
// shared: produced on Core 1 (thread context), consumed on Core 0; FrameRing release/acquire.
static pico::FrameRing<1024, 8> g_trace_ring;
static pico::FrameRing<128, 32> g_telemetry_ring_out;

static void wake_network_core() noexcept {
#ifdef PICO_ON_DEVICE
    __sev();
#endif
}

// Tracer fills a trace ring slot in place; Core 0 sends it ([SPEC-PICO-CORE-01], [SPEC-ARCH-08]).
// @impl [SPEC-PICO-CORE-01] [SPEC-TRACE-07] [SPEC-ARCH-08] targets/pico2w_rp2350/SPECIFICATION.md
class RingTraceTransport final : public trace::ITraceTransport {
public:
    std::span<uint8_t> acquire(size_t max_bytes) noexcept override {
        std::span<uint8_t> slot = g_trace_ring.acquire();
        if (slot.empty()) {
            g_trace_ring.note_drop();
            return {};
        }
        return slot.first(max_bytes < slot.size() ? max_bytes : slot.size());
    }
    void commit(std::span<uint8_t> buffer, size_t used_bytes) noexcept override {
        if (g_trace_ring.commit(buffer, used_bytes)) {
            wake_network_core();
        }
    }
};

static RingTraceTransport g_ring_trace_transport;

// @impl [SPEC-PICO-CORE-01] [SPEC-TRACE-07] targets/pico2w_rp2350/SPECIFICATION.md
trace::ITraceTransport* platform_trace_transport() noexcept {
#ifdef PICO_ON_DEVICE
    return &g_ring_trace_transport;
#else
    return nullptr;
#endif
}

// One telemetry frame per slot; Core 0 sends it ([SPEC-PICO-CORE-01]).
// @impl [SPEC-PICO-CORE-01] [SPEC-ARCH-08] targets/pico2w_rp2350/SPECIFICATION.md
void platform_send_telemetry(const uint8_t* data, size_t len) noexcept {
    if (g_telemetry_ring_out.push(data, len)) {
        wake_network_core();
    }
}

// Core 1: everything Wi-Fi related happens on Core 0 ([SPEC-PICO-CORE-01]).
// @impl [SPEC-PICO-CORE-01] targets/pico2w_rp2350/SPECIFICATION.md
// @impl [SPEC-PICO-CORE-01] targets/pico2w_rp2350/SPECIFICATION.md (wake from Core 0: __sev on every post)
static void pico_sev_wake() noexcept {
#ifdef PICO_ON_DEVICE
    __sev();
#endif
}

void platform_init() noexcept {
    abstractx::set_wake({&pico_sev_wake, &pico_sev_wake});
#ifdef PICO_ON_DEVICE
    printf("[Pico 2 W] AbstractX on core %u; Wi-Fi on core 0.\n", get_core_num());
#endif
}

// Core 1: no network work here; Core 0 polls CYW43 ([SPEC-PICO-CORE-01]).
// @impl [SPEC-PICO-CORE-01] targets/pico2w_rp2350/SPECIFICATION.md
// Core 1 has no network stack (Core 0 runs Wi-Fi); this is the per-step driver poll.
void platform_poll_network() noexcept {
    poll_pico_i2c_timeouts();   // async I2C deadlines ([SPEC-PICO-HAL-01])
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

} // namespace abstractx::hal

namespace abstractx {
// @impl [SPEC-ARCH-01] docs/DESIGN_SPECIFICATION.md (cross-core lock: core index for InterruptLock)
uint32_t abstractx_core_id() noexcept {
#ifdef PICO_ON_DEVICE
    return get_core_num();
#else
    return 0U;
#endif
}
} // namespace abstractx

#ifdef PICO_ON_DEVICE
extern "C" int __real_main();  // the application's main(), run on Core 1 (-Wl,--wrap=main)

// ---------------------------------------------------------------------------
// Core 0: Wi-Fi only ([SPEC-PICO-CORE-01], [SPEC-PICO-NET-01])
// ---------------------------------------------------------------------------
namespace {

using abstractx::hal::pico::WifiJoinAction;
using abstractx::hal::pico::WifiJoinMachine;

// shared: Core 0 only.
WifiJoinMachine g_wifi_join;
udp_pcb*        g_udp_pcb = nullptr;
ip_addr_t       g_dest_ip{};
constexpr uint16_t kTelemetryPort = 9870;

// shared: Core 1 stack; used only by Core 1 after launch.
alignas(8) uint32_t g_core1_stack[16 * 1024 / sizeof(uint32_t)];

uint32_t wifi_now_ms() noexcept {
    return to_ms_since_boot(get_absolute_time());
}

// @impl [SPEC-PICO-NET-01] targets/pico2w_rp2350/SPECIFICATION.md
void wifi_apply(const WifiJoinAction& a) noexcept {
    if (a.led_changed) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, a.led_on);
    }
    if (a.log_joined) {
        printf("[Pico 2 W Wi-Fi] Joined after %u attempt(s). IP: %s\n",
               static_cast<unsigned>(g_wifi_join.attempts()),
               ip4addr_ntoa(netif_ip4_addr(netif_default)));
    }
    if (a.log_failed) {
        printf("[Pico 2 W Wi-Fi] Attempt %u failed (link status %d); retrying in %u ms.\n",
               static_cast<unsigned>(g_wifi_join.attempts()), a.failed_status,
               static_cast<unsigned>(WifiJoinMachine::kRetryDelayMs));
    }
    if (a.log_lost) {
        printf("[Pico 2 W Wi-Fi] Link lost (status %d); rejoining.\n", a.failed_status);
    }
    if (a.leave) {
        cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    }
#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
    if (a.start_join) {
        if (cyw43_arch_wifi_connect_async(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK) != 0) {
            printf("[Pico 2 W Wi-Fi] connect_async refused; will retry.\n");
        }
    }
#endif
}

// Send every published frame of one ring as a PBUF_REF pbuf (no copy), then free the slot.
// @impl [SPEC-PICO-CORE-01] [SPEC-ARCH-08] targets/pico2w_rp2350/SPECIFICATION.md
template <typename Ring>
void drain_ring(Ring& ring, bool link_up) noexcept {
    for (std::span<const uint8_t> f = ring.peek(); !f.empty(); f = ring.peek()) {
        if (link_up && g_udp_pcb != nullptr) {
            pbuf* p = pbuf_alloc_reference(const_cast<uint8_t*>(f.data()),
                                           static_cast<u16_t>(f.size()), PBUF_REF);
            if (p != nullptr) {
                udp_sendto(g_udp_pcb, p, &g_dest_ip, kTelemetryPort);
                pbuf_free(p);
            }
        }
        ring.release();  // after udp_sendto returns: lwIP copies PBUF_REF data it queues
    }
}

void core1_entry() {
    (void)__real_main();
    while (true) {
        __wfe();
    }
}

// @impl [SPEC-PICO-CORE-01] [SPEC-PICO-NET-01] targets/pico2w_rp2350/SPECIFICATION.md
[[noreturn]] void network_core_loop() noexcept {
    bool wifi_ok = false;
    if (cyw43_arch_init() == 0) {
        cyw43_arch_enable_sta_mode();
        g_udp_pcb = udp_new();
        ip4addr_aton("255.255.255.255", &g_dest_ip);
        wifi_ok = true;
#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
        printf("[Pico 2 W Wi-Fi] Core %u: joining SSID '%s' in the background...\n",
               get_core_num(), WIFI_SSID);
        wifi_apply(g_wifi_join.start(wifi_now_ms()));
#else
        printf("[Pico 2 W Wi-Fi] STA mode ready (WIFI_SSID/WIFI_PASSWORD not set at compile time).\n");
#endif
    } else {
        printf("[Pico 2 W Wi-Fi] ERROR: cyw43_arch_init failed.\n");
    }

    while (true) {
        bool link_up = false;
        if (wifi_ok) {
            cyw43_arch_poll();
            const int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
            link_up = (status == CYW43_LINK_UP);
#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
            wifi_apply(g_wifi_join.step(wifi_now_ms(), status));
#endif
        }
        drain_ring(abstractx::hal::g_trace_ring, link_up);
        drain_ring(abstractx::hal::g_telemetry_ring_out, link_up);
        if (wifi_ok) {
            cyw43_arch_wait_for_work_until(make_timeout_time_ms(1));
        } else {
            best_effort_wfe_or_timeout(make_timeout_time_ms(1));
        }
    }
}

} // namespace

// The SDK starts this on Core 0 in place of the application's main ([SPEC-PICO-CORE-01]).
// @impl [SPEC-PICO-CORE-01] targets/pico2w_rp2350/SPECIFICATION.md
extern "C" int __wrap_main() {
    stdio_init_all();
    multicore_launch_core1_with_stack(core1_entry, g_core1_stack, sizeof(g_core1_stack));
    network_core_loop();
}
#endif
