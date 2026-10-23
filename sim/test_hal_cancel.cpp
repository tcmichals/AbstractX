/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: cancelled requests resume their coroutine with Cancelled [SPEC-HAL-07]
 * reset_queues() / cancel_pending() complete every queued request with its bus's Cancelled
 * status through the Dispatcher (never inline); an in-flight request either completes normally
 * (driver cannot abort) or is aborted with Cancelled. 1000 cancel cycles keep the coroutine
 * pool and dispatcher flat. Also checks the shared IoStatus mapping.
 */
// @verifies [SPEC-HAL-07] docs/DESIGN_SPECIFICATION.md
#undef NDEBUG

#include "abstractx/hal/i2c.hpp"
#include "abstractx/hal/spi.hpp"
#include "abstractx/hal/uart.hpp"
#include "asp_coro.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <optional>

using namespace abstractx::hal;
using abstractx::Dispatcher;
using abstractx::coro::Task;
using abstractx::coro::coro_pool_high_water;

namespace {

class FakeSpi : public ISpi {
public:
    explicit FakeSpi(bool can_abort) : can_abort_(can_abort) {}
    bool init(const SpiConfig&) override { return true; }
    void select(bool) override {}
    bool has_active() const { return active_valid_; }
    int starts{0};

    void complete_one() {
        SpiRequest req = active_;
        active_valid_ = false;
        SpiResult r{};
        r.transferred_bytes = req.tx_data.size();
        push_completion_from_isr(req, r);
        set_hardware_idle_from_isr();
    }

protected:
    void start_hardware_transfer_from_isr(const SpiRequest& req) noexcept override {
        active_ = req;
        active_valid_ = true;
        ++starts;
    }
    bool abort_active_from_isr() noexcept override {
        if (!can_abort_ || !active_valid_) return false;
        active_valid_ = false;
        push_completion_from_isr(active_, make_cancelled_result<SpiResult>());
        return true;
    }

private:
    bool can_abort_;
    SpiRequest active_{};
    bool active_valid_{false};
};

class FakeI2c : public II2c {
public:
    bool init(const I2cConfig&) override { return true; }
    bool write_read_sync(uint8_t, std::span<const uint8_t>, std::span<uint8_t>) override { return false; }
    bool write_sync(uint8_t, std::span<const uint8_t>) override { return false; }
    bool read_sync(uint8_t, std::span<uint8_t>) override { return false; }

protected:
    void start_hardware_transfer_from_isr(const I2cRequest&) noexcept override {}  // never completes
};

class FakeUart : public IUart {
public:
    void init(uint32_t) override {}
    void write_byte(uint8_t) override {}
    void puts(const char*) override {}
    size_t write(std::span<const uint8_t> b) override { return b.size(); }
    bool read_byte(uint8_t&) override { return false; }
    size_t read(std::span<uint8_t>) override { return 0; }
    void flush() override {}

protected:
    void start_hardware_transfer_from_isr(const UartTxRequest&) noexcept override {}  // never completes
};

const std::array<uint8_t, 4> kTx{1, 2, 3, 4};

Task<void> spi_xfer(FakeSpi& bus, SpiResult& out) {
    std::array<uint8_t, 4> rx{};
    out = co_await bus.transfer_async(kTx, rx);
}

Task<void> i2c_xfer(FakeI2c& bus, I2cResult& out) {
    std::array<uint8_t, 2> rx{};
    out = co_await bus.transfer_async(uint8_t{0x1E}, kTx, rx);
}

Task<void> uart_xfer(FakeUart& u, UartResult& out) { out = co_await u.write_async(kTx); }

void drain() {
    while (Dispatcher::has_pending_resumes()) Dispatcher::process();
}

constexpr size_t kN = 5;

// One cycle: kN requests (1 in flight, kN-1 queued), cancel, check, finish.
void cycle(FakeSpi& bus, bool expect_abort) {
    std::array<SpiResult, kN> res{};
    std::array<std::optional<Task<void>>, kN> t{};
    for (size_t i = 0; i < kN; ++i) {
        res[i].status = SpiStatus::BusError;  // sentinel
        t[i].emplace(spi_xfer(bus, res[i]));
        t[i]->resume();
    }
    assert(bus.has_active());
    bus.reset_queues();
    for (size_t i = 0; i < kN; ++i) assert(!t[i]->done());   // posted, not resumed inline
    drain();
    for (size_t i = 1; i < kN; ++i) assert(t[i]->done() && res[i].status == SpiStatus::Cancelled);
    if (expect_abort) {
        assert(!bus.has_active() && t[0]->done() && res[0].status == SpiStatus::Cancelled);
    } else {
        assert(bus.has_active() && !t[0]->done());           // DMA still owns the frame's buffer
        bus.complete_one();
        drain();
        assert(t[0]->done() && res[0].status == SpiStatus::Ok && res[0].transferred_bytes == 4);
    }
    assert(!bus.has_pending_requests());
}

} // namespace

int main() {
    // Shared vocabulary.
    static_assert(to_io_status(SpiStatus::Cancelled) == IoStatus::Cancelled);
    static_assert(to_io_status(I2cStatus::Cancelled) == IoStatus::Cancelled);
    static_assert(to_io_status(UartStatus::Cancelled) == IoStatus::Cancelled);
    static_assert(to_io_status(SpiStatus::QueueFull) == to_io_status(I2cStatus::QueueFull));
    static_assert(to_io_status(UartStatus::QueueFull) == IoStatus::QueueFull);
    static_assert(to_io_status(SpiStatus::CrcError) == IoStatus::Crc);
    static_assert(to_io_status(UartStatus::ParityError) == IoStatus::Parity);
    static_assert(make_cancelled_result<I2cResult>().status == I2cStatus::Cancelled);

    // 1. Driver cannot abort: queued cancelled, in-flight completes normally.
    FakeSpi plain(false);
    cycle(plain, false);
    // 2. Driver can abort: everything Cancelled.
    FakeSpi abortable(true);
    cycle(abortable, true);

    // 3. Same path on I2C and UART transmit (the shared base).
    {
        FakeI2c i2c;
        std::array<I2cResult, 3> r{};
        std::array<std::optional<Task<void>>, 3> t{};
        for (size_t i = 0; i < 3; ++i) { t[i].emplace(i2c_xfer(i2c, r[i])); t[i]->resume(); }
        assert(i2c.cancel_pending() == 2);
        assert(!i2c.cancel_active());   // default: cannot abort
        drain();
        assert(t[1]->done() && r[1].status == I2cStatus::Cancelled);
        assert(t[2]->done() && r[2].status == I2cStatus::Cancelled && !t[0]->done());
        // t[0] is "in flight" on a fake that keeps no pointer to it, so destroying it here is safe.
        FakeUart u;
        std::array<UartResult, 2> ur{};
        std::array<std::optional<Task<void>>, 2> ut{};
        for (size_t i = 0; i < 2; ++i) { ut[i].emplace(uart_xfer(u, ur[i])); ut[i]->resume(); }
        u.reset_queues();
        drain();
        assert(ut[1]->done() && ur[1].status == UartStatus::Cancelled);
    }

    // 4. Steady state: 1000 cancel cycles on each driver kind.
    const size_t hw0 = coro_pool_high_water();
    const uint32_t dropped0 = Dispatcher::dropped_resumes();
    for (int i = 0; i < 1000; ++i) {
        cycle(plain, false);
        cycle(abortable, true);
    }
    assert(coro_pool_high_water() == hw0);
    assert(Dispatcher::dropped_resumes() == dropped0);
    assert(Dispatcher::queue_depth() == 0);
    assert(plain.starts == 1001 && abortable.starts == 1001);   // queued requests never started

    std::printf("[PASS] HAL cancel: queued requests resume with Cancelled via the Dispatcher; in-flight completes or aborts; 1000 cycles flat\n");
    return 0;
}
