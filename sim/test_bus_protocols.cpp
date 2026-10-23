/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: bus protocol coverage (design option C, docs/design/bus-transaction-model.md)
 *   [SPEC-HAL-12] step callbacks: flash busy poll, repeat limit, SMBus count, Finish, Fail
 *   [SPEC-HAL-13] triggered reads into a zero-copy ring, overflow, a producer thread
 *   [SPEC-HAL-14] bus lock: other devices wait, FIFO order, cancel
 *   [SPEC-HAL-08] / [SPEC-HAL-09] / [SPEC-HAL-10] new device, segment, flag and line options
 * Fake drivers run the shared BusStepSequencer the way a target interrupt handler would.
 * Every scenario that loops runs 1000 times with the coroutine pool and dispatcher flat.
 */
// @verifies [SPEC-HAL-08] [SPEC-HAL-09] [SPEC-HAL-10] [SPEC-HAL-12] [SPEC-HAL-13] [SPEC-HAL-14] docs/DESIGN_SPECIFICATION.md
#undef NDEBUG

#include "abstractx/hal/i2c.hpp"
#include "abstractx/hal/platform.hpp"
#include "abstractx/hal/spi.hpp"
#include "abstractx/hal/uart.hpp"
#include "asp_coro.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace abstractx::hal;
using abstractx::Dispatcher;
using abstractx::coro::Task;
using abstractx::coro::coro_pool_high_water;

namespace {

// ---------------------------------------------------------------------------------------------
// Fake SPI bus: a SPI NOR flash (WREN 0x06, PROGRAM 0x02, RDSR 0x05) and a FIFO sensor
// (level 0x3A, then payload). Runs each transaction step by step through BusStepSequencer.
// ---------------------------------------------------------------------------------------------
class FakeSpi : public ISpi {
public:
    explicit FakeSpi(SpiCapabilities caps) : caps_(caps) {}
    bool init(const SpiConfig&) override { return true; }
    void select(bool) override {}
    SpiCapabilities capabilities() const noexcept override { return caps_; }

    bool has_active() const { return active_valid_; }
    int  starts{0};
    int  cs_releases{0};
    bool cs_asserted{false};
    std::array<uint8_t, 64> cs_log{};
    size_t cs_log_n{0};

    int     busy_polls{0};      // RDSR answers busy this many times (-1 = forever)
    int     rdsr_count{0};
    int     programs{0};
    uint8_t fifo_level{0};
    uint8_t header{0xA5};
    int     payload_runs{0};

    std::atomic<int> trigger_bus_reads{0};
    TriggerRingBase* trig_ring{nullptr};
    TriggerSource    trig_src{};

    void complete_one() {
        assert(active_valid_);
        SpiRequest req = active_;
        active_valid_ = false;
        SpiResult res{};
        assert(req.device != nullptr);
        cs_asserted = true;
        if (cs_log_n < cs_log.size()) cs_log[cs_log_n++] = req.device->cs;
        BusStepSequencer seq;
        seq.start(req.segments.size());
        BusStepNext next{};
        while (!next.done) {
            const SpiSegment& sg = req.segments[seq.index()];   // read when the step starts
            model(sg);
            res.transferred_bytes += sg.bytes();
            next = seq.step_done(sg.on_done, sg.rx);
        }
        cs_asserted = false;      // every end, early or not, releases CS
        ++cs_releases;
        res.status = spi_status_from(next.status);
        res.segments_done = seq.steps_done();
        push_completion_from_isr(req, res);
        set_hardware_idle_from_isr();
    }

    // The trigger's interrupt: take a slot, "DMA" into it, publish it. False = ring full.
    bool fire(uint64_t ts, uint32_t seq) {
        std::span<uint8_t> slot = trig_ring->acquire_from_isr();
        if (slot.empty()) return false;
        trigger_bus_reads.fetch_add(1, std::memory_order_relaxed);
        for (size_t i = 0; i < slot.size(); ++i) slot[i] = static_cast<uint8_t>(seq + i);
        std::memcpy(slot.data(), &seq, sizeof(seq) <= slot.size() ? sizeof(seq) : slot.size());
        trig_ring->commit_from_isr(IoStatus::Ok, slot.size(), ts);
        return true;
    }

protected:
    void start_hardware_transfer_from_isr(const SpiRequest& req) noexcept override {
        assert(!active_valid_);
        active_ = req;
        active_valid_ = true;
        ++starts;
    }
    SpiStatus start_triggered_read(const SpiDevice&, std::span<const uint8_t>, SpiLanes,
                                   const TriggerSource& src, TriggerRingBase& ring) noexcept override {
        trig_ring = &ring;
        trig_src = src;
        return SpiStatus::Ok;
    }
    void stop_triggered_read() noexcept override { trig_ring = nullptr; }

private:
    void model(const SpiSegment& sg) {
        const uint8_t cmd = sg.tx.empty() ? 0x00 : sg.tx[0];
        if (cmd == 0x05 && sg.rx.size() == 2) {            // RDSR: status in byte 1, bit 0 = busy
            ++rdsr_count;
            const bool busy = busy_polls < 0 || busy_polls-- > 0;
            sg.rx[1] = busy ? 0x01 : 0x00;
        } else if (cmd == 0x02) {
            ++programs;
        } else if (cmd == 0x3A && sg.rx.size() == 2) {     // FIFO level + header byte
            sg.rx[0] = header;
            sg.rx[1] = fifo_level;
        } else if (sg.tx.empty() && !sg.rx.empty()) {      // payload read
            ++payload_runs;
            for (size_t i = 0; i < sg.rx.size(); ++i) sg.rx[i] = static_cast<uint8_t>(0xC0 + i);
        }
    }

    SpiCapabilities caps_;
    SpiRequest active_{};
    bool active_valid_{false};
};

SpiCapabilities spi_caps() {
    SpiCapabilities c{};
    c.max_hz = 50'000'000;
    c.fifo_bytes = 64;
    c.max_segments = 4;
    c.num_cs = 4;
    c.modes = spi_mode_bit(SpiMode::Mode0);
    c.lanes = spi_lane_bit(SpiLanes::Single) | spi_lane_bit(SpiLanes::Dual);
    c.cs_release_between_segments = true;
    c.step_hooks = true;
    c.trigger_kinds = trigger_kind_bit(TriggerKind::PinRising) | trigger_kind_bit(TriggerKind::Periodic);
    return c;
}

const SpiDevice kFlash{0, SpiMode::Mode0, 50'000'000};
const SpiDevice kSensor{1, SpiMode::Mode0, 10'000'000};
const SpiDevice kOther2{2, SpiMode::Mode0, 1'000'000};
const SpiDevice kOther3{3, SpiMode::Mode0, 1'000'000};

void pump(FakeSpi& bus) {
    while (bus.has_active() || Dispatcher::has_pending_resumes()) {
        if (bus.has_active()) bus.complete_one();
        Dispatcher::process();
    }
}

// Hooks ([SPEC-HAL-12]).
BusStep flash_ready(void*, size_t, std::span<const uint8_t> rx) noexcept {
    return (rx[1] & 0x01) != 0 ? BusStep::Repeat : BusStep::Continue;
}

struct FifoCtx {
    std::array<SpiSegment, 2>* segs{nullptr};
    bool bad_header{false};
};
BusStep fifo_level(void* ctx, size_t, std::span<const uint8_t> rx) noexcept {
    auto* c = static_cast<FifoCtx*>(ctx);
    if (rx[0] != 0xA5) {
        c->bad_header = true;
        return BusStep::Fail;
    }
    if (rx[1] == 0) return BusStep::Finish;           // nothing waiting: skip the payload
    (*c->segs)[1].rx = (*c->segs)[1].rx.first(rx[1] < (*c->segs)[1].rx.size() ? rx[1] : (*c->segs)[1].rx.size());
    return BusStep::Continue;
}

// Flash page program in one transaction: WREN | PROGRAM | poll RDSR until ready.
Task<void> flash_program(FakeSpi& bus, uint16_t max_repeats, SpiResult& out) {
    std::array<uint8_t, 1> wren{0x06};
    std::array<uint8_t, 6> prog{0x02, 0x00, 0x10, 0x00, 0xAB, 0xCD};
    std::array<uint8_t, 2> rdsr{0x05, 0xFF};
    std::array<uint8_t, 2> status{};
    std::array<SpiSegment, 3> segs{SpiSegment::write(wren), SpiSegment::write(prog),
                                   SpiSegment::duplex(rdsr, status)};
    segs[0].release_cs_after = true;
    segs[1].release_cs_after = true;
    segs[2].on_done = BusStepHook{&flash_ready, nullptr, max_repeats};
    out = co_await bus.transfer_async(kFlash, segs);
}

Task<void> fifo_read(FakeSpi& bus, SpiResult& out, size_t& got, bool& bad) {
    std::array<uint8_t, 2> cmd{0x3A, 0xFF};
    std::array<uint8_t, 2> lvl{};
    std::array<uint8_t, 32> payload{};
    std::array<SpiSegment, 2> segs{SpiSegment::duplex(cmd, lvl), SpiSegment::read(payload)};
    FifoCtx ctx{&segs};
    segs[0].on_done = BusStepHook{&fifo_level, &ctx, 0};
    out = co_await bus.transfer_async(kSensor, segs);
    got = segs[1].rx.size();
    bad = ctx.bad_header;
}

Task<void> one(FakeSpi& bus, const SpiDevice& dev, std::span<const SpiSegment> segs, SpiResult& out) {
    out = co_await bus.transfer_async(dev, segs);
}

// A gate the test opens by hand: lets a coroutine hold the lock while others queue.
struct Gate {
    std::coroutine_handle<> h{};
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> hh) noexcept { h = hh; }
    void await_resume() const noexcept {}
    void open() { (void)Dispatcher::push(h); }
};

std::array<uint8_t, 1> g_byte{0x00};
const std::array<SpiSegment, 1> g_plain{SpiSegment::write(g_byte)};

Task<void> holder(FakeSpi& bus, Gate* gate, const SpiDevice& dev, SpiResult& out, bool& held) {
    BusLockGuard g = co_await bus.lock_async();
    held = g.held();
    if (!held) co_return;
    if (gate != nullptr) co_await *gate;
    out = co_await bus.transfer_async(dev, g_plain).under(g);
    SpiResult second = co_await bus.transfer_async(dev, g_plain).under(g);
    assert(second.status == SpiStatus::Ok);
}

// ---------------------------------------------------------------------------------------------
// Fake I2C controller: an SMBus battery at 0x0B (block read 0x20), an EEPROM at 0x50.
// ---------------------------------------------------------------------------------------------
class FakeI2c : public II2c {
public:
    explicit FakeI2c(I2cCapabilities caps) : caps_(caps) {}
    bool init(const I2cConfig&) override { return true; }
    bool write_read_sync(uint8_t, std::span<const uint8_t>, std::span<uint8_t>) override { return false; }
    bool write_sync(uint8_t, std::span<const uint8_t>) override { return false; }
    bool read_sync(uint8_t, std::span<uint8_t>) override { return false; }
    I2cCapabilities capabilities() const noexcept override { return caps_; }

    bool has_active() const { return active_valid_; }
    int starts{0};
    uint32_t last_hz{0};
    uint8_t block_len{5};
    std::array<uint8_t, 8> last_write{};
    size_t last_write_n{0};

    void complete_one() {
        assert(active_valid_);
        I2cRequest req = active_;
        active_valid_ = false;
        last_hz = req.hz;
        I2cResult res{};
        BusStepSequencer seq;
        seq.start(req.msgs.size());
        BusStepNext next{};
        uint8_t cmd = 0;
        last_write_n = 0;
        while (!next.done) {
            const I2cMsg& m = req.msgs[seq.index()];
            if (!m.is_read()) {
                for (uint8_t b : m.tx) {
                    if (last_write_n < last_write.size()) last_write[last_write_n++] = b;
                }
                cmd = m.tx.empty() ? cmd : m.tx[0];
                res.transferred_bytes += m.tx.size();
            } else if ((m.flags & I2cFlag::RecvLen) != 0) {
                m.rx[0] = block_len;
                for (size_t i = 0; i < block_len; ++i) m.rx[i + 1] = static_cast<uint8_t>(0x40 + i);
                res.transferred_bytes += 1U + block_len;
            } else if (cmd == 0x20 && m.rx.size() == 1) {   // block count
                m.rx[0] = block_len;
                cmd = 0x21;
                res.transferred_bytes += 1;
            } else {
                for (size_t i = 0; i < m.rx.size(); ++i) m.rx[i] = static_cast<uint8_t>(0x40 + i);
                res.transferred_bytes += m.rx.size();
            }
            next = seq.step_done(m.on_done, m.rx);
        }
        res.status = i2c_status_from(next.status);
        res.msgs_done = static_cast<uint8_t>(seq.steps_done());
        push_completion_from_isr(req, res);
        set_hardware_idle_from_isr();
    }

protected:
    void start_hardware_transfer_from_isr(const I2cRequest& req) noexcept override {
        assert(!active_valid_);
        active_ = req;
        active_valid_ = true;
        ++starts;
    }

private:
    I2cCapabilities caps_;
    I2cRequest active_{};
    bool active_valid_{false};
};

void pump(FakeI2c& bus) {
    while (bus.has_active() || Dispatcher::has_pending_resumes()) {
        if (bus.has_active()) bus.complete_one();
        Dispatcher::process();
    }
}

I2cCapabilities i2c_caps() {
    I2cCapabilities c{};
    c.max_hz = 1'000'000;
    c.max_msgs = 4;
    c.no_start = false;
    c.zero_length_write = true;
    c.per_device_hz = true;
    c.ignore_nack = true;
    c.recv_len = true;
    c.step_hooks = true;
    return c;
}

struct SmbusCtx {
    std::array<I2cMsg, 3>* msgs{nullptr};
};
BusStep smbus_count(void* ctx, size_t, std::span<const uint8_t> rx) noexcept {
    auto* c = static_cast<SmbusCtx*>(ctx);
    if (rx[0] == 0) return BusStep::Finish;
    if (rx[0] > kI2cSmbusBlockMax) return BusStep::Fail;
    (*c->msgs)[2].rx = (*c->msgs)[2].rx.first(rx[0]);
    return BusStep::Continue;
}

// SMBus block read written with a step hook: cmd, count, then exactly `count` bytes.
Task<void> smbus_block(FakeI2c& bus, I2cResult& out, size_t& got) {
    std::array<uint8_t, 1> cmd{0x20};
    std::array<uint8_t, 1> count{};
    std::array<uint8_t, 32> data{};
    std::array<I2cMsg, 3> msgs{I2cMsg::write(cmd), I2cMsg::read(count), I2cMsg::read(data)};
    SmbusCtx ctx{&msgs};
    msgs[1].on_done = BusStepHook{&smbus_count, &ctx, 0};
    out = co_await bus.transfer_async(I2cDevice{0x0B, 100'000}, msgs);
    got = msgs[2].rx.size();
    if (out.status == I2cStatus::Ok && out.msgs_done == 3) {
        for (size_t i = 0; i < got; ++i) assert(data[i] == 0x40 + i);
    }
}

Task<void> i2c_one(FakeI2c& bus, uint16_t addr, std::span<const I2cMsg> msgs, I2cResult& out) {
    out = co_await bus.transfer_async(addr, msgs);
}

Task<void> eeprom16(FakeI2c& bus, I2cResult& w, I2cResult& r) {
    const std::array<uint8_t, 2> in{0x11, 0x22};
    w = co_await bus.write_regs16_async(0x50, 0x1234, in);
    std::array<uint8_t, 3> out{};
    r = co_await bus.read_regs16_async(0x50, 0xABCD, out);
}

// ---------------------------------------------------------------------------------------------
// Fake UART for the break ordering test.
// ---------------------------------------------------------------------------------------------
class FakeUart : public IUart {
public:
    explicit FakeUart(UartCapabilities caps) : caps_(caps) {}
    void init(uint32_t) override {}
    void write_byte(uint8_t) override {}
    void puts(const char*) override {}
    size_t write(std::span<const uint8_t> b) override { return b.size(); }
    bool read_byte(uint8_t&) override { return false; }
    size_t read(std::span<uint8_t>) override { return 0; }
    void flush() override {}
    UartCapabilities capabilities() const noexcept override { return caps_; }

    bool has_active() const { return active_valid_; }
    std::array<uint32_t, 8> log{};   // data size, or 0x80000000 | break_us
    size_t log_n{0};

    void complete_one() {
        UartTxRequest req = active_;
        active_valid_ = false;
        log[log_n++] = req.break_us != 0 ? (0x80000000U | req.break_us) : static_cast<uint32_t>(req.data.size());
        push_completion_from_isr(req, UartResult{UartStatus::Ok, req.data.size()});
        set_hardware_idle_from_isr();
    }

protected:
    void start_hardware_transfer_from_isr(const UartTxRequest& req) noexcept override {
        active_ = req;
        active_valid_ = true;
    }

private:
    UartCapabilities caps_;
    UartTxRequest active_{};
    bool active_valid_{false};
};

Task<void> uart_seq(FakeUart& u, UartResult& b) {
    const std::array<uint8_t, 2> ab{'A', 'B'};
    const std::array<uint8_t, 1> c{'C'};
    (void)co_await u.write_async(ab);
    b = co_await u.send_break_async(100);
    (void)co_await u.write_async(c);
}

Task<void> uart_break(FakeUart& u, uint32_t us, UartResult& b) { b = co_await u.send_break_async(us); }

struct HookLog {
    std::array<uint8_t, 1024> seen{};
    size_t n{0};
};
void rx_hook(void* ctx, std::span<const uint8_t> a, std::span<const uint8_t> b) noexcept {
    auto* h = static_cast<HookLog*>(ctx);
    for (uint8_t x : a) h->seen[h->n++] = x;
    for (uint8_t x : b) h->seen[h->n++] = x;
}

// Trigger consumer: pops until cancelled; checks order.
Task<void> trigger_consumer(TriggerRingBase& ring, uint32_t& received, uint32_t& reported_drops, bool& ordered) {
    uint32_t last = 0;
    bool first = true;
    for (;;) {
        TriggerSample s = co_await ring.pop();
        if (s.status == IoStatus::Cancelled) co_return;
        uint32_t seq = 0;
        std::memcpy(&seq, s.data.data(), sizeof(seq));
        ordered = ordered && (first || seq > last) && s.timestamp_us == seq * 125U && s.data.size() == 16;
        first = false;
        last = seq;
        ++received;
        reported_drops += s.dropped;
        ring.release();
    }
}

} // namespace

int main() {
    // ================= [SPEC-HAL-12] step callbacks =================
    {
        // 1. Flash program + busy poll in one transaction.
        FakeSpi bus(spi_caps());
        bus.busy_polls = 3;
        SpiResult r{};
        auto t = flash_program(bus, 10, r);
        t.resume();
        pump(bus);
        assert(t.done() && r.status == SpiStatus::Ok && r.segments_done == 3);
        assert(bus.rdsr_count == 4 && bus.programs == 1 && !bus.cs_asserted && bus.cs_releases == 1);

        // 2. Dead flash: Repeat beyond max_repeats ends with Timeout, CS released.
        bus.busy_polls = -1;
        bus.rdsr_count = 0;
        auto d = flash_program(bus, 5, r);
        d.resume();
        pump(bus);
        assert(d.done() && r.status == SpiStatus::Timeout && to_io_status(r.status) == IoStatus::Timeout);
        assert(bus.rdsr_count == 6 && r.segments_done == 2 && !bus.cs_asserted);

        // 3. FIFO level 0: Finish skips the payload, Ok with 1 segment done.
        size_t got = 99;
        bool bad = false;
        bus.fifo_level = 0;
        auto f0 = fifo_read(bus, r, got, bad);
        f0.resume();
        pump(bus);
        assert(f0.done() && r.status == SpiStatus::Ok && r.segments_done == 1 && bus.payload_runs == 0);

        // 4. FIFO level 7: the hook shortens the payload read to 7 bytes.
        bus.fifo_level = 7;
        auto f7 = fifo_read(bus, r, got, bad);
        f7.resume();
        pump(bus);
        assert(f7.done() && r.status == SpiStatus::Ok && r.segments_done == 2 && got == 7);
        assert(bus.payload_runs == 1 && r.transferred_bytes == 2 + 7);

        // 5. Bad header: Fail ends with Aborted, nothing after it runs, CS released.
        bus.header = 0x00;
        auto fb = fifo_read(bus, r, got, bad);
        fb.resume();
        pump(bus);
        assert(fb.done() && r.status == SpiStatus::Aborted && to_io_status(r.status) == IoStatus::Aborted);
        assert(bad && r.segments_done == 0 && bus.payload_runs == 1 && !bus.cs_asserted);
        bus.header = 0xA5;

        // 6. A driver without step hooks refuses a hooked transaction before the hardware.
        SpiCapabilities no_hooks = spi_caps();
        no_hooks.step_hooks = false;
        FakeSpi plain(no_hooks);
        auto n = flash_program(plain, 3, r);
        n.resume();
        assert(n.done() && r.status == SpiStatus::NotSupported && plain.starts == 0);

        // 7. Steady state: 1000 stepped transactions of each kind.
        bus.busy_polls = 0;
        {   // warm-up: one round of the loop body before the high-water mark is taken
            auto w1 = flash_program(bus, 8, r);
            w1.resume();
            pump(bus);
            auto w2 = fifo_read(bus, r, got, bad);
            w2.resume();
            pump(bus);
        }
        const size_t hw0 = coro_pool_high_water();
        const uint32_t dq0 = Dispatcher::dropped_resumes();
        for (int i = 0; i < 1000; ++i) {
            bus.busy_polls = i % 4;
            auto a = flash_program(bus, 8, r);
            a.resume();
            pump(bus);
            assert(a.done() && r.status == SpiStatus::Ok);
            bus.fifo_level = static_cast<uint8_t>(i % 3);
            auto b = fifo_read(bus, r, got, bad);
            b.resume();
            pump(bus);
            assert(b.done() && r.status == SpiStatus::Ok);
        }
        assert(coro_pool_high_water() == hw0 && Dispatcher::dropped_resumes() == dq0);
        assert(!bus.has_pending_requests() && Dispatcher::queue_depth() == 0 && !bus.cs_asserted);
    }

    // ================= [SPEC-HAL-08] device and segment attributes =================
    {
        FakeSpi bus(spi_caps());
        SpiResult r{};
        auto check = [&](const SpiDevice& dev, std::span<const SpiSegment> segs, SpiStatus want) {
            auto t = one(bus, dev, segs, r);
            t.resume();
            pump(bus);
            assert(t.done() && r.status == want);
        };
        std::array<uint8_t, 3> three{};
        std::array<uint8_t, 4> four{};
        std::array<uint8_t, 4> four_in{};
        SpiSegment w9 = SpiSegment::write(three);
        w9.word_bits = 9;                                   // 9-bit words take 2 bytes: 3 is not whole
        check(kFlash, std::array<SpiSegment, 1>{w9}, SpiStatus::InvalidArgument);
        SpiSegment w16 = SpiSegment::write(four);
        w16.word_bits = 16;
        check(kFlash, std::array<SpiSegment, 1>{w16}, SpiStatus::NotSupported);   // no 16-bit words
        SpiSegment w0 = SpiSegment::write(four);
        w0.word_bits = 0;
        check(kFlash, std::array<SpiSegment, 1>{w0}, SpiStatus::InvalidArgument);
        SpiSegment hz = SpiSegment::write(four);
        hz.hz = 1'000'000;
        check(kFlash, std::array<SpiSegment, 1>{hz}, SpiStatus::NotSupported);    // no per-segment clock
        SpiSegment dummy = SpiSegment::write(four);
        dummy.dummy_cycles = 8;
        check(kFlash, std::array<SpiSegment, 1>{dummy}, SpiStatus::NotSupported);
        SpiSegment dc = SpiSegment::write(four);
        dc.dc_level = 1;
        check(kFlash, std::array<SpiSegment, 1>{dc}, SpiStatus::InvalidArgument);   // device has no D/C pin
        SpiDevice lcd{2, SpiMode::Mode0, 20'000'000};
        lcd.dc_pin = 12;
        check(lcd, std::array<SpiSegment, 1>{dc}, SpiStatus::NotSupported);         // controller has none
        SpiDevice slow_cs = kOther3;
        slow_cs.cs_setup_ns = 500;
        check(slow_cs, g_plain, SpiStatus::NotSupported);
        SpiDevice wire3 = kOther3;
        wire3.three_wire = true;
        check(wire3, g_plain, SpiStatus::NotSupported);
        assert(bus.starts == 0);

        SpiCapabilities rich = spi_caps();
        rich.word_bits = (1UL << 8) | (1UL << 15) | (1UL << 23);   // 9, 16, 24 bits
        rich.per_segment_hz = true;
        rich.max_dummy_cycles = 16;
        rich.cs_timing = true;
        rich.max_cs_delay_ns = 10'000;
        rich.three_wire = true;
        rich.dc_pin = true;
        FakeSpi bus2(rich);
        auto check2 = [&](const SpiDevice& dev, std::span<const SpiSegment> segs, SpiStatus want) {
            auto t = one(bus2, dev, segs, r);
            t.resume();
            pump(bus2);
            assert(t.done() && r.status == want);
        };
        check2(kFlash, std::array<SpiSegment, 1>{w16}, SpiStatus::Ok);
        SpiSegment w24 = SpiSegment::write(four);
        w24.word_bits = 24;                                 // 24-bit words take 4 bytes
        check2(kFlash, std::array<SpiSegment, 1>{w24}, SpiStatus::Ok);
        check2(kFlash, std::array<SpiSegment, 1>{hz}, SpiStatus::Ok);
        hz.hz = 80'000'000;                                 // above the controller maximum
        check2(kFlash, std::array<SpiSegment, 1>{hz}, SpiStatus::NotSupported);
        check2(kFlash, std::array<SpiSegment, 1>{dummy}, SpiStatus::Ok);
        check2(lcd, std::array<SpiSegment, 1>{dc}, SpiStatus::Ok);
        check2(slow_cs, g_plain, SpiStatus::Ok);
        check2(wire3, std::array<SpiSegment, 1>{SpiSegment::duplex(four, four_in)}, SpiStatus::InvalidArgument);
        check2(wire3, std::array<SpiSegment, 1>{SpiSegment::read(four_in)}, SpiStatus::Ok);
        assert(SpiDevice{}.fill == 0xFF);
    }

    // ================= [SPEC-HAL-14] bus lock =================
    {
        FakeSpi bus(spi_caps());
        // 1. Another device's transaction waits until the holder releases.
        Gate gate;
        SpiResult ra{}, rb{};
        bool held = false;
        auto a = holder(bus, &gate, kFlash, ra, held);
        a.resume();
        assert(held && bus.bus_lock().held());
        auto b = one(bus, kSensor, g_plain, rb);
        b.resume();
        assert(!b.done() && bus.starts == 0 && bus.bus_lock().has_waiters());
        gate.open();
        pump(bus);
        assert(a.done() && b.done() && ra.status == SpiStatus::Ok && rb.status == SpiStatus::Ok);
        assert(bus.cs_log_n == 3 && bus.cs_log[0] == 0 && bus.cs_log[1] == 0 && bus.cs_log[2] == 1);
        assert(!bus.bus_lock().held() && !bus.bus_lock().has_waiters());

        // 2. First-come order: A holds; B waits (transaction); C waits (lock); D waits (transaction).
        bus.cs_log_n = 0;
        Gate g2;
        SpiResult rc{}, rd{};
        bool held_c = false;
        auto a2 = holder(bus, &g2, kFlash, ra, held);
        a2.resume();
        auto b2 = one(bus, kSensor, g_plain, rb);
        b2.resume();
        auto c2 = holder(bus, nullptr, kOther2, rc, held_c);
        c2.resume();
        auto d2 = one(bus, kOther3, g_plain, rd);
        d2.resume();
        assert(!b2.done() && !c2.done() && !d2.done() && bus.starts == 3);
        g2.open();
        pump(bus);
        assert(a2.done() && b2.done() && c2.done() && d2.done() && held_c);
        // A, A, then B was submitted before C took the lock; C, C; then D.
        const std::array<uint8_t, 6> want{0, 0, 1, 2, 2, 3};
        assert(bus.cs_log_n == 6);
        for (size_t i = 0; i < want.size(); ++i) assert(bus.cs_log[i] == want[i]);

        // 3. Cancel: a waiting transaction gets Cancelled, a waiting lock request an empty guard.
        Gate g3;
        bool held_x = true;
        auto a3 = holder(bus, &g3, kFlash, ra, held);
        a3.resume();
        auto b3 = one(bus, kSensor, g_plain, rb);
        b3.resume();
        auto c3 = holder(bus, nullptr, kOther2, rc, held_x);
        c3.resume();
        bus.cancel_lock_waiters();
        Dispatcher::process();
        assert(b3.done() && rb.status == SpiStatus::Cancelled && to_io_status(rb.status) == IoStatus::Cancelled);
        assert(c3.done() && !held_x);
        g3.open();
        pump(bus);
        assert(a3.done() && ra.status == SpiStatus::Ok && !bus.bus_lock().held());

        // 4. Steady state: 1000 lock cycles with a waiting transaction each time.
        {   // warm-up round
            Gate g;
            auto h = holder(bus, &g, kFlash, ra, held);
            h.resume();
            auto w = one(bus, kSensor, g_plain, rb);
            w.resume();
            g.open();
            pump(bus);
        }
        const size_t hw0 = coro_pool_high_water();
        const uint32_t dq0 = Dispatcher::dropped_resumes();
        for (int i = 0; i < 1000; ++i) {
            Gate g;
            auto h = holder(bus, &g, kFlash, ra, held);
            h.resume();
            auto w = one(bus, kSensor, g_plain, rb);
            w.resume();
            g.open();
            pump(bus);
            assert(h.done() && w.done() && rb.status == SpiStatus::Ok);
        }
        assert(coro_pool_high_water() == hw0 && Dispatcher::dropped_resumes() == dq0);
        assert(!bus.bus_lock().held() && !bus.bus_lock().has_waiters() && Dispatcher::queue_depth() == 0);
    }

    // ================= [SPEC-HAL-13] triggered reads =================
    {
        static TriggerRing<16, 4> ring;
        static const std::array<uint8_t, 1> imu_cmd{0x80 | 0x1D};
        FakeSpi none({});
        assert(none.arm_triggered_read(kSensor, imu_cmd, SpiLanes::Single, TriggerSource{}, ring) ==
               SpiStatus::NotSupported);
        FakeSpi bus(spi_caps());
        assert(bus.arm_triggered_read(kSensor, imu_cmd, SpiLanes::Single,
                                      TriggerSource{TriggerKind::PinBoth, 5}, ring) == SpiStatus::NotSupported);
        assert(bus.arm_triggered_read(kSensor, imu_cmd, SpiLanes::Single,
                                      TriggerSource{TriggerKind::Periodic, 0, 0}, ring) == SpiStatus::InvalidArgument);
        assert(bus.arm_triggered_read(SpiDevice{9, SpiMode::Mode0, 1'000'000}, imu_cmd, SpiLanes::Single,
                                      TriggerSource{TriggerKind::PinRising, 5}, ring) == SpiStatus::NotSupported);
        assert(bus.arm_triggered_read(kSensor, imu_cmd, SpiLanes::Single,
                                      TriggerSource{TriggerKind::PinRising, 5}, ring) == SpiStatus::Ok);
        assert(bus.arm_triggered_read(kSensor, imu_cmd, SpiLanes::Single,
                                      TriggerSource{TriggerKind::PinRising, 5}, ring) == SpiStatus::InvalidArgument);
        assert(bus.trig_ring == &ring && bus.trig_src.pin == 5);

        // 1. Overflow: 6 events, 4 slots: 2 dropped and the bus untouched for them.
        for (uint32_t i = 1; i <= 6; ++i) (void)bus.fire(i * 125U, i);
        assert(bus.trigger_bus_reads.load() == 4 && ring.dropped() == 2 && ring.depth() == 4);
        uint32_t received = 0, drops = 0;
        bool ordered = true;
        auto c = trigger_consumer(ring, received, drops, ordered);
        c.resume();
        Dispatcher::process();
        while (Dispatcher::has_pending_resumes()) Dispatcher::process();
        assert(received == 4 && drops == 0 && ordered && ring.depth() == 0);
        (void)bus.fire(7 * 125U, 7);                       // the next sample reports the 2 drops
        while (Dispatcher::has_pending_resumes()) Dispatcher::process();
        assert(received == 5 && drops == 2);

        // 2. Steady state + a producer thread (TSan): 20000 events, every one received or dropped.
        const size_t hw0 = coro_pool_high_water();
        const uint32_t dq0 = Dispatcher::dropped_resumes();
        const uint32_t dropped0 = ring.dropped();
        received = 0;
        drops = 0;
        std::atomic<bool> done{false};
        std::thread producer([&] {
            for (uint32_t i = 100; i < 20100; ++i) {
                (void)bus.fire(i * 125U, i);
                if ((i & 63U) == 0) std::this_thread::yield();
            }
            done.store(true, std::memory_order_release);
        });
        while (!c.done()) {
            Dispatcher::process();
            if (done.load(std::memory_order_acquire) && ring.depth() == 0) ring.cancel();
            std::this_thread::yield();
        }
        producer.join();
        assert(ordered && received + (ring.dropped() - dropped0) == 20000);
        assert(coro_pool_high_water() == hw0 && Dispatcher::dropped_resumes() == dq0);
        bus.disarm_triggered_read();
        assert(bus.trig_ring == nullptr);
        assert(bus.arm_triggered_read(kSensor, imu_cmd, SpiLanes::Dual,
                                      TriggerSource{TriggerKind::Periodic, 0, 125}, ring) == SpiStatus::Ok);
        bus.disarm_triggered_read();

        // I2C arming checks.
        FakeI2c i2c(i2c_caps());
        static const std::array<uint8_t, 1> reg{0x03};
        assert(i2c.arm_triggered_read(I2cDevice{0x1E}, reg, TriggerSource{}, ring) == I2cStatus::NotSupported);
        assert(i2c.arm_triggered_read(I2cDevice{0x99}, reg, TriggerSource{}, ring) == I2cStatus::InvalidArgument);
    }

    // ================= [SPEC-HAL-09] I2C device speed, flags, 16-bit registers, PEC =================
    {
        FakeI2c bus(i2c_caps());
        I2cResult r{};
        size_t got = 0;
        // SMBus block read with a step hook: count 5 -> exactly 5 data bytes.
        auto s = smbus_block(bus, r, got);
        s.resume();
        pump(bus);
        assert(s.done() && r.status == I2cStatus::Ok && r.msgs_done == 3 && got == 5 && bus.last_hz == 100'000);
        // count 0 -> Finish after the count; count 40 -> Fail (Aborted).
        bus.block_len = 0;
        auto s0 = smbus_block(bus, r, got);
        s0.resume();
        pump(bus);
        assert(s0.done() && r.status == I2cStatus::Ok && r.msgs_done == 2);
        bus.block_len = 40;
        auto s40 = smbus_block(bus, r, got);
        s40.resume();
        pump(bus);
        assert(s40.done() && r.status == I2cStatus::Aborted && to_io_status(r.status) == IoStatus::Aborted);

        // RecvLen: needs a 33-byte buffer; the controller reads count + data.
        bus.block_len = 4;
        std::array<uint8_t, 1> cmd{0x20};
        std::array<uint8_t, 33> blk{};
        std::array<uint8_t, 8> small{};
        const std::array<I2cMsg, 2> rl{I2cMsg::write(cmd), I2cMsg::read(blk, I2cFlag::RecvLen)};
        auto t = i2c_one(bus, 0x0B, rl, r);
        t.resume();
        pump(bus);
        assert(t.done() && r.status == I2cStatus::Ok && blk[0] == 4 && r.transferred_bytes == 1 + 1 + 4);
        const std::array<I2cMsg, 2> rl_small{I2cMsg::write(cmd), I2cMsg::read(small, I2cFlag::RecvLen)};
        auto t2 = i2c_one(bus, 0x0B, rl_small, r);
        t2.resume();
        assert(t2.done() && r.status == I2cStatus::InvalidArgument);
        const std::array<I2cMsg, 1> rl_write{I2cMsg::write(cmd, I2cFlag::RecvLen)};
        auto t3 = i2c_one(bus, 0x0B, rl_write, r);
        t3.resume();
        assert(t3.done() && r.status == I2cStatus::InvalidArgument);

        // Capability gates.
        FakeI2c basic([] { I2cCapabilities c{}; c.max_hz = 400'000; c.max_msgs = 2; return c; }());
        auto g1 = i2c_one(basic, 0x0B, rl, r);
        g1.resume();
        assert(g1.done() && r.status == I2cStatus::NotSupported);
        const std::array<I2cMsg, 1> ign{I2cMsg::write(cmd, I2cFlag::IgnoreNack)};
        auto g2 = i2c_one(basic, 0x0B, ign, r);
        g2.resume();
        assert(g2.done() && r.status == I2cStatus::NotSupported);
        auto g3 = [](FakeI2c& b, I2cResult& o) -> Task<void> {
            std::array<uint8_t, 1> c1{0x01};
            const std::array<I2cMsg, 1> m{I2cMsg::write(c1)};
            o = co_await b.transfer_async(I2cDevice{0x0B, 100'000}, m);
        }(basic, r);
        g3.resume();
        assert(g3.done() && r.status == I2cStatus::NotSupported && basic.starts == 0);
        auto g4 = [](FakeI2c& b, I2cResult& o) -> Task<void> {
            std::array<uint8_t, 1> c1{0x01};
            const std::array<I2cMsg, 1> m{I2cMsg::write(c1)};
            o = co_await b.transfer_async(I2cDevice{0x0B, 3'400'000}, m);   // above controller max
        }(bus, r);
        g4.resume();
        assert(g4.done() && r.status == I2cStatus::NotSupported);

        // 16-bit register helpers: address high byte first; write combined (no NoStart support).
        I2cResult w{};
        auto e = eeprom16(bus, w, r);
        e.resume();
        pump(bus);
        assert(e.done() && w.status == I2cStatus::Ok && r.status == I2cStatus::Ok);
    assert(w.transferred_bytes == 4 && r.transferred_bytes == 2 + 3);   // {12 34 11 22}; {AB CD} + 3
        assert(bus.last_write_n == 2 && bus.last_write[0] == 0xAB && bus.last_write[1] == 0xCD);  // read: reg only

        // PEC: CRC-8/SMBUS check value of "123456789" is 0xF4.
        const std::array<uint8_t, 9> check{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
        static_assert(smbus_pec(0, std::span<const uint8_t>()) == 0);
        assert(smbus_pec(0, check) == 0xF4);
        assert(smbus_pec(smbus_pec(0, std::span<const uint8_t>(check).first(4)), std::span<const uint8_t>(check).subspan(4)) == 0xF4);

        // Steady state: 1000 stepped SMBus reads.
        bus.block_len = 5;
        {   // warm-up round
            auto k = smbus_block(bus, r, got);
            k.resume();
            pump(bus);
        }
        const size_t hw0 = coro_pool_high_water();
        const uint32_t dq0 = Dispatcher::dropped_resumes();
        for (int i = 0; i < 1000; ++i) {
            auto k = smbus_block(bus, r, got);
            k.resume();
            pump(bus);
            assert(k.done() && r.status == I2cStatus::Ok && got == 5);
        }
        assert(coro_pool_high_water() == hw0 && Dispatcher::dropped_resumes() == dq0);
    }

    // ================= [SPEC-HAL-10] UART line options, break, data hook, ports =================
    {
        UartCapabilities none{};
        UartConfig inv{};
        inv.invert_rx = true;
        UartConfig hd{};
        hd.half_duplex = true;
        UartConfig de{};
        de.rs485_de_pin = 7;
        for (const auto& c : {inv, hd, de}) assert(check_uart_config(none, c) == UartStatus::NotSupported);
        UartCapabilities rich{};
        rich.invert = true;
        rich.half_duplex = true;
        rich.rs485_de = true;
        rich.send_break = true;
        rich.max_break_us = 1000;
        for (const auto& c : {inv, hd, de}) assert(check_uart_config(rich, c) == UartStatus::Ok);

        FakeUart plain(none);
        UartResult b{};
        auto r0 = uart_break(plain, 100, b);
        r0.resume();
        assert(r0.done() && b.status == UartStatus::NotSupported && !plain.has_active());
        FakeUart u(rich);
        auto r1 = uart_break(u, 0, b);
        r1.resume();
        assert(r1.done() && b.status == UartStatus::InvalidArgument);
        auto r2 = uart_break(u, 5000, b);
        r2.resume();
        assert(r2.done() && b.status == UartStatus::NotSupported && !u.has_active());
        auto q = uart_seq(u, b);
        q.resume();
        while (u.has_active() || Dispatcher::has_pending_resumes()) {
            if (u.has_active()) u.complete_one();
            Dispatcher::process();
        }
        assert(q.done() && b.status == UartStatus::Ok && u.log_n == 3);
        assert(u.log[0] == 2 && u.log[1] == (0x80000000U | 100U) && u.log[2] == 1);   // in order

        // Data hook sees every byte exactly once, across the wrap, with no reader.
        static UartRxRing<64, 4> rx;
        static HookLog hook;
        rx.set_data_hook(&rx_hook, &hook);
        std::span<uint8_t> mem = rx.dma_region();
        uint32_t wpos = 0;
        for (int chunk = 0; chunk < 10; ++chunk) {
            for (int i = 0; i < 50; ++i, ++wpos) mem[wpos % mem.size()] = static_cast<uint8_t>(wpos);
            rx.produced_from_isr(50, chunk % 3 == 0);
        }
        assert(hook.n == 500);
        for (size_t i = 0; i < hook.n; ++i) assert(hook.seen[i] == static_cast<uint8_t>(i));

        // Ports: 0 is the default driver; the host target has no second port.
        assert(get_uart_port(0) == &get_uart_driver());
        assert(get_uart_port(1) == nullptr);
    }

    std::printf("[PASS] bus protocols: step callbacks (poll, finish, fail, repeat limit, SMBus), "
                "segment/device attributes, bus lock FIFO + cancel, triggered reads + threaded producer, "
                "I2C device speed/RecvLen/16-bit regs/PEC, UART line options/break/data hook/ports; 1000-round loops flat\n");
    return 0;
}
