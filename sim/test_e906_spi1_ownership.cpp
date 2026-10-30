/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: SPI1 has one owner at a time [SPEC-E906-HAL-03]
 * Compiles the real targets/allwinner_e906/src/spi.cpp against fake registers
 * (sim/e906_shim/memory_map.h) and a fake DMA controller. FIFO, DMA, sync and coroutine
 * paths must claim the bus; a refused claim fails at once and never steals another
 * transfer's completion. Coroutines are started once and pumped through the Dispatcher.
 */
// @verifies [SPEC-E906-HAL-03] targets/allwinner_e906/SPECIFICATION.md
#undef NDEBUG  // the checks below must run in Release builds too

#include "hal/spi.hpp"
#include "hal/ccu.hpp"
#include "hal/dma.hpp"
#include "hal/timer.hpp"
#include "asp_coro.hpp"
#include "abstractx/domain_dispatcher.hpp"

#include <cassert>
#include <cstring>
#include <iostream>

unsigned char g_fake_spi1_regs[0x400];
unsigned char g_fake_pio_regs[0x400];

using abstractx::Dispatcher;
using abstractx::coro::Task;
using fc::hal::Spi1;

namespace {

constexpr size_t kTcr = 0x08;
constexpr size_t kFcr = 0x18;

volatile uint32_t &reg(size_t off) {
    return *reinterpret_cast<volatile uint32_t *>(g_fake_spi1_regs + off);
}

struct FakeDma {
    int next_channel = 8;
    ::hal::DmaController::CompletionCallback cb[16]{};
    bool running[16]{};
} g_dma;

void (*g_tick_hook)() = nullptr;  // runs once inside the next Timer::get_ticks()
uint64_t g_ticks = 0;

// Completes the one running SPI1 DMA (the RX channel carries the callback).
void complete_dma(bool ok = true) {
    for (uint8_t ch = 0; ch < 16; ++ch) {
        if (g_dma.running[ch] && g_dma.cb[ch].is_valid()) {
            auto cb = g_dma.cb[ch];
            cb(ch, ok);
            return;
        }
    }
    assert(!"no DMA in flight");
}

void pump() {
    while (Dispatcher::has_pending_resumes()) Dispatcher::process();
}

uint8_t g_tx[128];
uint8_t g_rx[128];

} // namespace

namespace hal {
void Ccu::enable_module(BusModule) noexcept {}
void DmaController::init() noexcept {}
int DmaController::allocate_channel() noexcept { return g_dma.next_channel++; }
bool DmaController::start_transfer(uint8_t ch, const DmaLli *, CompletionCallback cb, uint8_t, bool) noexcept {
    g_dma.cb[ch] = cb;
    g_dma.running[ch] = true;
    return true;
}
void DmaController::stop_transfer(uint8_t ch) noexcept { g_dma.running[ch] = false; }
// Each call is one timer read; the hardware finishes a FIFO exchange (XCH self-clears) after it.
uint64_t Timer::get_ticks() noexcept {
    ++g_ticks;
    if (auto hook = g_tick_hook) {
        g_tick_hook = nullptr;
        hook();
    }
    reg(kTcr) = reg(kTcr) & ~(1U << 31);
    return g_ticks;
}
} // namespace hal

namespace {

void test_fifo_refused_while_dma_in_flight() {
    assert(Spi1::start_dma_transfer(0, g_tx, g_rx, 100));
    reg(kFcr) = 0xABCDU;  // the running DMA's FIFO setup must survive
    assert(!Spi1::transceive_fifo_sync(0, g_tx, g_rx, 4));
    assert(reg(kFcr) == 0xABCDU);
    complete_dma();
    assert(!Spi1::is_busy());
    assert(Spi1::transceive_fifo_sync(0, g_tx, g_rx, 4));
    assert(!Spi1::is_busy());
}

bool g_hook_started_dma = false;

void test_dma_refused_during_fifo() {
    // A DRDY edge in the middle of a FIFO exchange tries to start its auto-DMA.
    g_hook_started_dma = false;
    g_tick_hook = [] { g_hook_started_dma = Spi1::start_dma_transfer(0, g_tx, g_rx, 15); };
    assert(Spi1::transceive_fifo_sync(0, g_tx, g_rx, 4));
    assert(!g_hook_started_dma);
    assert(!Spi1::is_busy());
}

void test_sync_dma_refused_while_other_dma_in_flight() {
    assert(Spi1::start_dma_transfer(0, g_tx, g_rx, 100));
    assert(!Spi1::transceive_imu_single_sync(g_tx, g_rx, 100));
    assert(!Spi1::transceive_fpga_dual_sync(g_tx, g_rx, 100));
    complete_dma();
    assert(!Spi1::is_busy());
}

Task<void> waiter(bool &result, bool &done) {
    result = co_await Spi1::async_transceive_imu_single(g_tx, g_rx, 100);
    done = true;
}

void test_refused_awaiter_does_not_steal_completion() {
    bool res_a = false, done_a = false, res_b = true, done_b = false;
    auto a = waiter(res_a, done_a);
    a.resume();  // start once: suspends with its DMA in flight
    assert(!done_a && Spi1::is_busy());
    auto b = waiter(res_b, done_b);
    b.resume();  // bus busy: refused, finishes with false
    assert(done_b && !res_b);
    complete_dma();
    pump();
    assert(done_a && res_a);
    assert(!Spi1::is_busy());
}

Task<void> looper(int rounds, int &ok) {
    for (int i = 0; i < rounds; ++i) {
        if (co_await Spi1::async_transceive_imu_single(g_tx, g_rx, 100)) ++ok;
    }
}

void test_steady_state_1000_rounds() {
    int ok = 0;
    auto t = looper(1000, ok);
    t.resume();
    complete_dma();
    pump();
    const size_t hw0 = abstractx::coro::coro_pool_high_water();
    const uint32_t dropped0 = Dispatcher::dropped_resumes();
    for (int i = 1; i < 1000; ++i) {
        complete_dma();
        pump();
    }
    assert(ok == 1000 && t.done());
    assert(abstractx::coro::coro_pool_high_water() == hw0);
    assert(Dispatcher::dropped_resumes() == dropped0);
}

} // namespace

int main(int argc, char **argv) {
    struct Case { const char *name; void (*fn)(); };
    const Case cases[] = {
        {"fifo_refused_while_dma_in_flight", test_fifo_refused_while_dma_in_flight},
        {"dma_refused_during_fifo", test_dma_refused_during_fifo},
        {"sync_dma_refused", test_sync_dma_refused_while_other_dma_in_flight},
        {"refused_awaiter", test_refused_awaiter_does_not_steal_completion},
        {"steady_state", test_steady_state_1000_rounds},
    };
    Spi1::init(10'000'000);
    for (const Case &c : cases) {
        if (argc > 1 && std::strcmp(argv[1], c.name) != 0) continue;  // one case by name
        c.fn();
        std::cout << "PASS " << c.name << "\n";
    }
    std::cout << "[PASS] SPI1 one owner: FIFO, DMA, sync and awaiter paths\n";
    return 0;
}
