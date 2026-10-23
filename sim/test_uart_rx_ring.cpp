/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: UART receive ring with zero-copy spans [SPEC-HAL-10]
 * A fake UART owns a 4 x 64 B ring that the test fills the way a circular DMA would.
 * Checks: default driver refuses; min-bytes wake; partial frame on RX timeout; wrap into two
 * spans; deadline timeout (stale token ignored); cancel; one reader; overrun before and while
 * held; line-error counters; 1000-frame steady state; a producer thread (for TSan).
 */
// @verifies [SPEC-HAL-10] docs/DESIGN_SPECIFICATION.md
#undef NDEBUG

#include "abstractx/hal/uart.hpp"
#include "asp_coro.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>

using namespace abstractx::hal;
using abstractx::Dispatcher;
using abstractx::coro::Task;
using abstractx::coro::coro_pool_high_water;

namespace {

class FakeUart : public IUart {
public:
    explicit FakeUart(bool with_ring, bool with_timer) : with_ring_(with_ring) {
        if (with_timer) ring_.set_deadline_hook(&FakeUart::arm, this);
    }
    void init(uint32_t) override {}
    void write_byte(uint8_t) override {}
    void puts(const char*) override {}
    size_t write(std::span<const uint8_t> b) override { return b.size(); }
    bool read_byte(uint8_t&) override { return false; }
    size_t read(std::span<uint8_t>) override { return 0; }
    void flush() override {}
    UartRxRingBase* rx_ring() noexcept override { return with_ring_ ? &ring_ : nullptr; }

    // "DMA": write bytes at the ring's write position, then the package-end / RX-timeout IRQ.
    void dma(std::span<const uint8_t> bytes, bool eof) {
        auto mem = ring_.dma_region();
        for (uint8_t b : bytes) mem[wr_++ % mem.size()] = b;
        ring_.produced_from_isr(bytes.size(), eof);
    }
    void dma_seq(size_t n, bool eof) {   // bytes = running sequence number
        auto mem = ring_.dma_region();
        for (size_t i = 0; i < n; ++i) { mem[wr_ % mem.size()] = static_cast<uint8_t>(wr_); ++wr_; }
        ring_.produced_from_isr(n, eof);
    }

    UartRxRing<64, 4> ring_;
    uint32_t armed_us{0};
    uint32_t armed_token{0};
    uint32_t wr_{0};

protected:
    void start_hardware_transfer_from_isr(const UartTxRequest&) noexcept override {}

private:
    static void arm(void* ctx, uint32_t us, uint32_t token) {
        auto* self = static_cast<FakeUart*>(ctx);
        self->armed_us = us;
        self->armed_token = token;
    }
    bool with_ring_;
};

void drain() {
    while (Dispatcher::has_pending_resumes()) Dispatcher::process();
}

UartRxView g_view{};
Task<void> reader(FakeUart& u, size_t min, uint32_t to) { g_view = co_await u.read_async(min, to); }

bool view_is(const UartRxView& v, uint8_t first_value, size_t n) {
    if (v.size() != n) return false;
    size_t i = 0;
    for (uint8_t b : v.first) if (b != static_cast<uint8_t>(first_value + i++)) return false;
    for (uint8_t b : v.second) if (b != static_cast<uint8_t>(first_value + i++)) return false;
    return true;
}

} // namespace

int main() {
    // 1. Default driver (no ring): NotSupported; argument and capability refusals.
    {
        FakeUart none(false, false);
        auto t = reader(none, 1, 0);
        t.resume();
        assert(t.done() && g_view.status == UartStatus::NotSupported);
        assert(none.release_rx(1) == UartStatus::NotSupported);

        FakeUart no_timer(true, false);
        auto a = reader(no_timer, 0, 0);
        a.resume();
        assert(a.done() && g_view.status == UartStatus::InvalidArgument);
        auto b = reader(no_timer, 193, 0);    // > ring minus the DMA buffer
        b.resume();
        assert(b.done() && g_view.status == UartStatus::InvalidArgument);
        auto c = reader(no_timer, 1, 1000);   // deadline without a timer: refused, not faked
        c.resume();
        assert(c.done() && g_view.status == UartStatus::NotSupported);
    }

    FakeUart u(true, true);

    // 2. min_bytes wake.
    {
        auto t = reader(u, 10, 0);
        t.resume();
        u.dma_seq(4, false);
        drain();
        assert(!t.done());
        u.dma_seq(6, false);
        assert(!t.done());          // posted, not resumed inside the "ISR"
        drain();
        assert(t.done() && g_view.status == UartStatus::Ok && view_is(g_view, 0, 10));
        assert(u.release_rx(10) == UartStatus::Ok);
    }

    // 3. Partial frame on RX timeout (end_of_frame) below min_bytes.
    {
        auto t = reader(u, 50, 0);
        t.resume();
        u.dma_seq(7, true);
        drain();
        assert(t.done() && g_view.status == UartStatus::Ok && view_is(g_view, 10, 7));
        assert(u.release_rx(3) == UartStatus::Ok);       // partial release: rest offered again
        auto t2 = reader(u, 1, 0);
        t2.resume();
        assert(t2.done() && view_is(g_view, 13, 4));
        assert(u.release_rx(4) == UartStatus::Ok);
        assert(u.release_rx(1) == UartStatus::InvalidArgument);   // nothing held
    }

    // 4. Wrap: 17 consumed so far; fill to 250, read across the end in two spans.
    {
        u.dma_seq(233, false);
        auto t = reader(u, 1, 0);
        t.resume();
        assert(t.done() && view_is(g_view, 17, 233));
        assert(u.release_rx(233) == UartStatus::Ok);
        u.dma_seq(20, true);    // positions 250..269 -> wraps at 256
        auto t2 = reader(u, 20, 0);
        t2.resume();
        assert(t2.done() && g_view.first.size() == 6 && g_view.second.size() == 14 && view_is(g_view, 250 & 0xFF, 20));
        assert(g_view.second.data() == u.ring_.dma_region().data());   // zero copy: points into the ring
        assert(u.release_rx(20) == UartStatus::Ok);
    }

    // 5. Deadline: Timeout with what arrived; a stale token is ignored.
    {
        auto t = reader(u, 20, 5000);
        t.resume();
        assert(!t.done() && u.armed_us == 5000);
        const uint32_t tok = u.armed_token;
        u.ring_.deadline_from_isr(tok - 1);   // stale
        drain();
        assert(!t.done());
        u.dma_seq(3, false);
        u.ring_.deadline_from_isr(tok);
        drain();
        assert(t.done() && g_view.status == UartStatus::Timeout && g_view.size() == 3);
        assert(u.release_rx(3) == UartStatus::Ok);
    }

    // 6. Cancel, and one reader per port.
    {
        auto t = reader(u, 5, 0);
        t.resume();
        UartRxView second{};
        auto t2 = [](FakeUart& f, UartRxView& out) -> Task<void> { out = co_await f.read_async(1, 0); }(u, second);
        t2.resume();
        assert(t2.done() && second.status == UartStatus::QueueFull);
        u.cancel_read();
        assert(!t.done());
        drain();
        assert(t.done() && g_view.status == UartStatus::Cancelled && !u.ring_.reader_waiting());
    }

    // 7. Overrun before the reader looked: lost bytes counted and skipped, never delivered.
    {
        const uint32_t start = u.wr_;      // 273, nothing unreleased
        u.dma_seq(300, false);              // laps the 256 B ring
        const uint32_t w = u.wr_;
        const uint32_t floor = (w / 64U) * 64U + 64U - 256U;
        assert(u.rx_stats().dropped_bytes == floor - start);
        auto t = reader(u, 1, 0);
        t.resume();
        assert(t.done() && g_view.dropped == floor - start && view_is(g_view, static_cast<uint8_t>(floor), w - floor));
        assert(u.release_rx(g_view.size()) == UartStatus::Ok);

        // 8. Overrun while held: release reports OverrunError.
        u.dma_seq(10, true);
        auto t2 = reader(u, 1, 0);
        t2.resume();
        assert(t2.done() && g_view.size() == 10 && g_view.dropped == 0);
        u.dma_seq(256, false);            // DMA laps the bytes the reader is holding
        assert(u.release_rx(10) == UartStatus::OverrunError);
        auto t3 = reader(u, 1, 0);        // resynchronise past the lost region
        t3.resume();
        assert(t3.done());
        assert(u.release_rx(g_view.size()) == UartStatus::Ok);
    }

    // 9. Line errors: counted per kind and flagged in the next view.
    {
        u.ring_.line_error_from_isr(UartLineError::Framing | UartLineError::Parity);
        u.ring_.line_error_from_isr(UartLineError::Break);
        u.dma_seq(2, true);
        auto t = reader(u, 1, 0);
        t.resume();
        assert(t.done() && g_view.line_errors == (UartLineError::Framing | UartLineError::Parity | UartLineError::Break));
        assert(u.rx_stats().framing == 1 && u.rx_stats().parity == 1 && u.rx_stats().breaks == 1 && u.rx_stats().overrun == 0);
        assert(u.release_rx(2) == UartStatus::Ok);
    }

    // 10. Steady state: 1000 NMEA-sized frames delivered by RX timeout, no drops.
    {
        const uint32_t dropped_before = u.rx_stats().dropped_bytes;
        {
            auto w = reader(u, 1, 0);
            w.resume();
            u.dma_seq(37, true);
            drain();
            assert(w.done());
            assert(u.release_rx(g_view.size()) == UartStatus::Ok);
        }
        const size_t hw0 = coro_pool_high_water();
        const uint32_t dq0 = Dispatcher::dropped_resumes();
        for (int i = 0; i < 1000; ++i) {
            const uint8_t first = static_cast<uint8_t>(u.wr_);
            auto t = reader(u, 1, 0);
            t.resume();
            u.dma_seq(37, true);
            drain();
            assert(t.done() && g_view.status == UartStatus::Ok && view_is(g_view, first, 37) && g_view.dropped == 0);
            assert(u.release_rx(37) == UartStatus::Ok);
        }
        assert(coro_pool_high_water() == hw0 && Dispatcher::dropped_resumes() == dq0);
        assert(u.rx_stats().dropped_bytes == dropped_before && Dispatcher::queue_depth() == 0);
    }

    // 11. Producer on another thread (the "ISR"), reader coroutine on this one: 200 KB in
    //     order, no drops. The producer only writes where the reader has released (flow control
    //     standing in for a fast enough reader).
    {
        FakeUart t_uart(true, false);
        constexpr uint32_t kTotal = 200'000;
        std::atomic<uint32_t> released{0};
        std::atomic<bool> stop{false};
        std::thread producer([&] {
            uint32_t sent = 0;
            while (sent < kTotal) {
                const uint32_t chunk = 1 + (sent * 13U) % 50U;
                const uint32_t n = chunk < kTotal - sent ? chunk : kTotal - sent;
                while (sent + n - released.load(std::memory_order_acquire) > 192U - 64U) {
                    if (stop.load()) return;
                    std::this_thread::yield();
                }
                t_uart.dma_seq(n, true);
                sent += n;
            }
        });
        uint32_t got = 0;
        bool in_order = true;
        auto consumer = [](FakeUart& f, uint32_t total, uint32_t& n_got, bool& ordered,
                           std::atomic<uint32_t>& rel) -> Task<void> {
            while (n_got < total) {
                UartRxView v = co_await f.read_async(1, 0);
                for (auto part : {v.first, v.second}) {
                    for (uint8_t b : part) {
                        ordered = ordered && b == static_cast<uint8_t>(n_got);
                        ++n_got;
                    }
                }
                const bool rel_ok = f.release_rx(v.size()) == UartStatus::Ok && v.dropped == 0;
                ordered = ordered && rel_ok;
                rel.store(n_got, std::memory_order_release);
            }
        }(t_uart, kTotal, got, in_order, released);
        consumer.resume();
        while (!consumer.done()) {
            Dispatcher::process();
            std::this_thread::yield();
        }
        stop.store(true);
        producer.join();
        assert(got == kTotal && in_order && t_uart.rx_stats().dropped_bytes == 0);
    }

    std::printf("[PASS] UART RX ring: zero-copy spans, partial frames, timeout, cancel, overrun counted, 1000 frames flat, threaded producer\n");
    return 0;
}
