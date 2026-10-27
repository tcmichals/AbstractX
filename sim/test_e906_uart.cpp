/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * E906 UART driver (targets/allwinner_e906/src/uart.cpp) against a register model of the
 * DesignWare 8250 and a fake DMA controller. Runs on the host. Not covered here (the plain-memory
 * model cannot drop RFL on an RBR read): interrupt-mode receive and the async write request.
 *
 * @verifies [SPEC-E906-HAL-04] E906Uart init on_irq read write set_dma
 * @verifies [SPEC-E906-HAL-02] E906Uart dma_sync
 * @verifies [SPEC-HAL-15] UartPort
 */

#include "hal/uart.hpp"
#include "hal/driver_wait.hpp"
#include "hal/timer.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <span>

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

using abstractx::hal::E906Uart;
using abstractx::hal::UartBuffers;

// ---------------------------------------------------------------------------
// Fakes for the target services the driver calls.
// ---------------------------------------------------------------------------
namespace {
uint64_t g_ticks = 0;
int g_attached = 0;
bool g_dma_available = false;
uint32_t g_pkg = 0;        // DMAC_PKG_NUM_REG
uint32_t g_cur_dst = 0;    // DMAC_CUR_DEST_REG
int g_tx_dma_starts = 0;
::hal::DmaController::CompletionCallback g_tx_cb{};
}  // namespace

namespace hal {
uint64_t Timer::get_ticks() noexcept { return g_ticks; }
uint64_t Timer::get_time_us() noexcept { return g_ticks / 24ULL; }
void Timer::arm_wake(uint64_t) noexcept {}
void Timer::delay_us(uint32_t us) noexcept { g_ticks += us * TICKS_PER_US; }
void Ccu::enable_module(BusModule) noexcept {}
void DmaController::init() noexcept {}
int DmaController::allocate_channel() noexcept { return g_dma_available ? 8 : -1; }
void DmaController::stop_transfer(uint8_t) noexcept {}
bool DmaController::start_transfer(uint8_t ch, const DmaLli*, CompletionCallback cb, uint8_t, bool cyclic) noexcept {
    if (!cyclic) {  // TX
        ++g_tx_dma_starts;
        g_tx_cb = cb;
    }
    (void)ch;
    return true;
}
uint32_t DmaController::pkg_num(uint8_t) noexcept { return g_pkg; }
uint32_t DmaController::current_dst_pa(uint8_t) noexcept { return g_cur_dst; }
namespace irq {
bool attach(uint32_t, Handler, void*) noexcept { ++g_attached; return true; }
void describe(uint32_t, const char*, const volatile uint32_t*) noexcept {}
}  // namespace irq
}  // namespace hal

namespace abstractx::hal {
void platform_idle_wait() noexcept {}
}

// ---------------------------------------------------------------------------
// Register model
// ---------------------------------------------------------------------------
namespace {

constexpr size_t kRbr = 0x00 / 4, kIer = 0x04 / 4, kIir = 0x08 / 4, kLcr = 0x0C / 4,
                 kLsr = 0x14 / 4, kRfl = 0x84 / 4, kHalt = 0xA4 / 4, kDmaReqEn = 0xB4 / 4;

alignas(4) uint32_t regs[0x40];
uint32_t pin_cfg = 0, pull_cfg = 0;

const ::hal::ports::UartPort kPort{reinterpret_cast<uintptr_t>(regs), 37, ::hal::Ccu::BusModule::Uart3, 17,
                                   {reinterpret_cast<uintptr_t>(&pin_cfg), 0xFFU << 12, 0x22U << 12},
                                   {reinterpret_cast<uintptr_t>(&pull_cfg), 0x03U << 24, 0x01U << 24},
                                   128, "uart-test"};

alignas(4) uint8_t dma_rx[UartBuffers::kDmaRingBytes];
::hal::DmaLli rx_lli[UartBuffers::kDmaNumBufs];
uint8_t tx_dma[UartBuffers::kTxDmaBytes];
::hal::DmaLli tx_lli;
uint8_t rx_mem[UartBuffers::kRxBytes];
uint8_t tx_mem[UartBuffers::kTxBytes];

E906Uart dut{kPort, UartBuffers{dma_rx, rx_lli, tx_dma, &tx_lli, rx_mem, tx_mem}};

void bus_wait(uint64_t) noexcept { g_ticks += 24 * 100; }

}  // namespace

int main() {
    ::hal::set_driver_wait_hook(bus_wait);

    // ---- init: pins and pull of this port only, baud sequence, DMA unavailable -> interrupt RX
    regs[kIir] = 0x01;
    dut.init(115200);
    CHECK(pin_cfg == (0x22U << 12) && pull_cfg == (0x01U << 24));
    CHECK(regs[kLcr] == 0x03 && regs[kHalt] == 0x00);
    CHECK(regs[kIer] == (0x01 | 0x04));  // RX + line status
    CHECK(!dut.dma_active() && dut.attached() && g_attached == 1);
    dut.init(115200);
    CHECK(g_attached == 1);  // re-init never attaches twice

    // ---- shared line: nothing pending -> no register touched
    regs[kIir] = 0x01;
    regs[kRbr] = 0xEE;
    dut.on_irq();
    uint8_t probe[4];
    CHECK(dut.read(probe) == 0);

    // ---- TX through the ring + THR-empty interrupt
    {
        const uint8_t msg[] = {'$', 'G', 'P'};
        CHECK(dut.write(msg) == 3);
        CHECK(regs[kIer] & 0x02);  // THR-empty enabled
        regs[kIir] = 0x02;
        dut.on_irq();              // drains the 3 bytes (last one left in RBR)
        CHECK(regs[kRbr] == 'P');
        CHECK((regs[kIer] & 0x02) == 0);  // ring empty -> THR-empty disabled
        regs[kIir] = 0x01;
    }

    // ---- line status error reaches the core ring counters
    {
        regs[kLsr] = (1U << 1) | (1U << 3);  // overrun + framing
        regs[kIir] = 0x06;
        dut.on_irq();
        regs[kIir] = 0x01;
        const auto st = dut.rx_stats();  // the model's LSR does not clear on read: >= 1
        CHECK(st.overrun >= 1 && st.framing >= 1 && st.parity == 0);
        regs[kLsr] = 0;
    }

    // ---- DMA receive: the DMAC fills 64 B buffers; read() returns completed bytes in order
    g_dma_available = true;
    CHECK(dut.set_dma(true) && dut.dma_active());
    CHECK(regs[kDmaReqEn] == 0x07 && regs[kIir] == 0x8F);
    {
        const uint32_t base = ::hal::DmaController::pa(dma_rx);
        uint32_t written = 0;
        uint8_t next = 0, expect = 0;
        // 1000 frames of 37 bytes (steady state), each ended by an RX timeout (end of frame)
        for (int f = 0; f < 1000; ++f) {
            for (int i = 0; i < 37; ++i) {
                dma_rx[written % UartBuffers::kDmaRingBytes] = next++;
                ++written;
                g_pkg = written / UartBuffers::kDmaBufBytes;
            }
            g_cur_dst = base + (written % UartBuffers::kDmaRingBytes);
            regs[kRfl] = 0;
            regs[kIir] = 0x0C;
            // the handler returns after the guard; make IIR idle after the first pass
            dut.on_irq();
            regs[kIir] = 0x01;
            uint8_t buf[64];
            const size_t n = dut.read(buf);
            CHECK(n == 37);
            for (size_t i = 0; i < n; ++i) CHECK(buf[i] == expect++);
        }
        CHECK(dut.rx_dropped() == 0);
    }

    // ---- TX larger than the FIFO goes by DMA in 256 B chunks
    {
        std::array<uint8_t, 300> big{};
        g_tx_dma_starts = 0;
        // complete each chunk from the wait hook
        ::hal::set_driver_wait_hook(+[](uint64_t) noexcept {
            g_ticks += 24;
            if (g_tx_cb.is_valid()) { auto cb = g_tx_cb; g_tx_cb = {}; cb(8, true); }
        });
        CHECK(dut.write(big) == 300);
        CHECK(g_tx_dma_starts == 2);
        // stuck DMA: the 50 ms deadline stops it and reports what was sent
        ::hal::set_driver_wait_hook(bus_wait);
        CHECK(dut.write(big) == 0);
    }

    std::printf("PASS test_e906_uart\n");
    return 0;
}
