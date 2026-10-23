/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: SPI devices and segmented transactions on one bus driver [SPEC-HAL-08]
 * One fake bus driver serves three devices described only by SpiDevice + segments:
 * an IMU (register bursts), the FPGA Dual-SPI TLP link (1-lane command, 2-lane 64-byte data)
 * and a SPI NOR flash (dual / quad fast read). Requests outside the capabilities are refused
 * with NotSupported before the hardware; malformed ones with InvalidArgument; 1000 rounds
 * keep the coroutine pool, dispatcher and request queue flat.
 */
// @verifies [SPEC-HAL-08] docs/DESIGN_SPECIFICATION.md
#undef NDEBUG

#include "abstractx/hal/spi.hpp"
#include "asp_coro.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <optional>

using namespace abstractx::hal;
using abstractx::Dispatcher;
using abstractx::coro::Task;
using abstractx::coro::coro_pool_high_water;

namespace {

constexpr uint8_t kCsImu = 0;
constexpr uint8_t kCsFpga = 1;
constexpr uint8_t kCsFlash = 2;

// What the fake saw on the wire for the last transaction.
struct WireLog {
    uint8_t  cs{0xFF};
    SpiMode  mode{SpiMode::Mode0};
    size_t   segments{0};
    std::array<uint8_t, 4> lanes{};
    uint32_t sclk_cycles{0};
};

class FakeSpiBus : public ISpi {
public:
    explicit FakeSpiBus(SpiCapabilities caps) : caps_(caps) {}

    bool init(const SpiConfig&) override { return true; }
    void select(bool) override {}
    SpiCapabilities capabilities() const noexcept override { return caps_; }

    bool has_active() const { return active_valid_; }
    int  starts{0};
    WireLog log{};

    std::array<uint8_t, 16>  imu_regs{};
    std::array<uint8_t, 64>  fpga_egress{};   // TLP the FPGA will return on 0xA2
    std::array<uint8_t, 64>  fpga_ingress{};  // last TLP written with 0xA1

    // The "ISR": run the active transaction against the device models and complete it.
    void complete_one() {
        assert(active_valid_);
        SpiRequest req = active_;
        active_valid_ = false;
        SpiResult res{};
        if (req.device != nullptr) {
            res = run_transaction(*req.device, req.segments);
        } else {
            // Legacy single-buffer path (unchanged behaviour): echo tx into rx.
            for (size_t i = 0; i < req.rx_data.size(); ++i) {
                req.rx_data[i] = i < req.tx_data.size() ? req.tx_data[i] : 0xFF;
            }
            res.transferred_bytes = req.tx_data.size() > req.rx_data.size() ? req.tx_data.size() : req.rx_data.size();
        }
        push_completion_from_isr(req, res);
        set_hardware_idle_from_isr();
    }

protected:
    void start_hardware_transfer_from_isr(const SpiRequest& req) noexcept override {
        assert(!active_valid_);
        active_ = req;
        active_valid_ = true;
        ++starts;
    }

private:
    static uint8_t flash_byte(uint32_t addr) { return static_cast<uint8_t>((addr * 7U) ^ 0x5AU); }

    SpiResult run_transaction(const SpiDevice& dev, std::span<const SpiSegment> segs) {
        SpiResult res{};
        log = WireLog{};
        log.cs = dev.cs;
        log.mode = dev.mode;
        log.segments = segs.size();
        for (size_t i = 0; i < segs.size() && i < log.lanes.size(); ++i) {
            log.lanes[i] = static_cast<uint8_t>(segs[i].lanes);
        }
        for (const SpiSegment& s : segs) {
            log.sclk_cycles += static_cast<uint32_t>(s.bytes() * 8U / static_cast<uint8_t>(s.lanes));
            res.transferred_bytes += s.bytes();
        }
        const uint8_t cmd = segs[0].tx.empty() ? 0xFF : segs[0].tx[0];
        if (dev.cs == kCsImu && segs.size() >= 2) {   // register access: command + data
            const uint8_t reg = cmd & 0x7F;
            if ((cmd & 0x80) != 0) {
                for (size_t i = 0; i < segs[1].rx.size(); ++i) segs[1].rx[i] = imu_regs[(reg + i) & 0x0F];
            } else {
                for (size_t i = 0; i < segs[1].tx.size(); ++i) imu_regs[(reg + i) & 0x0F] = segs[1].tx[i];
            }
        } else if (dev.cs == kCsFpga) {
            // asp_spi_frontend.sv: command on IO0 (1 lane), then 64 bytes on IO0/IO1.
            assert(segs.size() == 2 && segs[0].lanes == SpiLanes::Single && segs[1].bytes() == 64);
            if (cmd == 0xA1) {
                std::memcpy(fpga_ingress.data(), segs[1].tx.data(), 64);
            } else if (cmd == 0xA2) {
                std::memcpy(segs[1].rx.data(), fpga_egress.data(), 64);
            }
        } else if (dev.cs == kCsFlash) {
            // 0x3B dual-output / 0x6B quad-output fast read: cmd, A23..A0, one dummy byte.
            assert(segs[0].tx.size() == 5 && (cmd == 0x3B || cmd == 0x6B));
            assert(segs[1].lanes == (cmd == 0x3B ? SpiLanes::Dual : SpiLanes::Quad));
            const uint32_t addr = (uint32_t{segs[0].tx[1]} << 16) | (uint32_t{segs[0].tx[2]} << 8) | segs[0].tx[3];
            for (size_t i = 0; i < segs[1].rx.size(); ++i) segs[1].rx[i] = flash_byte(addr + static_cast<uint32_t>(i));
        }
        return res;
    }

    SpiCapabilities caps_;
    SpiRequest active_{};
    bool active_valid_{false};
};

void pump(FakeSpiBus& bus) {
    while (bus.has_active() || Dispatcher::has_pending_resumes()) {
        if (bus.has_active()) bus.complete_one();
        Dispatcher::process();
    }
}

SpiCapabilities full_caps() {
    SpiCapabilities c{};
    c.max_hz = 50'000'000;
    c.fifo_bytes = 64;
    c.max_segments = 4;
    c.num_cs = 3;
    c.modes = spi_mode_bit(SpiMode::Mode0) | spi_mode_bit(SpiMode::Mode3);
    c.lanes = spi_lane_bit(SpiLanes::Single) | spi_lane_bit(SpiLanes::Dual) | spi_lane_bit(SpiLanes::Quad);
    c.dma = true;
    return c;
}

const SpiDevice kImu{kCsImu, SpiMode::Mode0, 24'000'000};
const SpiDevice kFpga{kCsFpga, SpiMode::Mode0, 50'000'000};
const SpiDevice kFlash{kCsFlash, SpiMode::Mode0, 50'000'000};

SpiResult g_res{};

// FPGA TLP write (0xA1) then read back (0xA2); egress returns ingress for the loopback check.
Task<void> fpga_roundtrip(FakeSpiBus& bus, uint8_t seed, bool& ok) {
    std::array<uint8_t, 1> wcmd{0xA1};
    std::array<uint8_t, 64> tlp{};
    for (size_t i = 0; i < tlp.size(); ++i) tlp[i] = static_cast<uint8_t>(seed + i);
    const std::array<SpiSegment, 2> wr{SpiSegment::write(wcmd), SpiSegment::write(tlp, SpiLanes::Dual)};
    SpiResult r = co_await bus.transfer_async(kFpga, wr);
    ok = r.status == SpiStatus::Ok && r.transferred_bytes == 65;
    bus.fpga_egress = bus.fpga_ingress;

    std::array<uint8_t, 1> rcmd{0xA2};
    std::array<uint8_t, 64> back{};
    const std::array<SpiSegment, 2> rd{SpiSegment::write(rcmd), SpiSegment::read(back, SpiLanes::Dual)};
    r = co_await bus.transfer_async(kFpga, rd);
    ok = ok && r.status == SpiStatus::Ok && back == tlp;
    g_res = r;
}

Task<void> flash_reads(FakeSpiBus& bus, uint32_t addr, bool& ok) {
    std::array<uint8_t, 5> cmd{0x3B, static_cast<uint8_t>(addr >> 16), static_cast<uint8_t>(addr >> 8),
                               static_cast<uint8_t>(addr), 0x00};
    std::array<uint8_t, 256> page{};
    SpiResult r = co_await bus.write_then_read_async(kFlash, cmd, page, SpiLanes::Dual);
    ok = r.status == SpiStatus::Ok && r.transferred_bytes == 261;
    for (size_t i = 0; i < page.size(); ++i) ok = ok && page[i] == static_cast<uint8_t>(((addr + i) * 7U) ^ 0x5AU);

    cmd[0] = 0x6B;  // quad output
    const std::array<SpiSegment, 2> q{SpiSegment::write(cmd), SpiSegment::read(page, SpiLanes::Quad)};
    r = co_await bus.transfer_async(kFlash, q);
    ok = ok && r.status == SpiStatus::Ok;
    for (size_t i = 0; i < page.size(); ++i) ok = ok && page[i] == static_cast<uint8_t>(((addr + i) * 7U) ^ 0x5AU);
}

Task<void> imu_regs(FakeSpiBus& bus, bool& ok) {
    const std::array<uint8_t, 3> cfg{0x11, 0x22, 0x33};
    SpiResult w = co_await bus.write_regs_async(kImu, 0x84, cfg);   // write mask clears bit 7 -> reg 4
    std::array<uint8_t, 3> back{};
    SpiResult r = co_await bus.read_regs_async(kImu, 0x04, back);
    ok = w.status == SpiStatus::Ok && w.transferred_bytes == 4 && r.status == SpiStatus::Ok &&
         back == cfg && bus.imu_regs[4] == 0x11;
}

Task<void> one(FakeSpiBus& bus, const SpiDevice& dev, std::span<const SpiSegment> segs, SpiResult& out) {
    out = co_await bus.transfer_async(dev, segs);
}

} // namespace

int main() {
    std::array<uint8_t, 1> cmd{0xA1};
    std::array<uint8_t, 64> data{};
    std::array<uint8_t, 64> in{};
    const std::array<SpiSegment, 2> dual{SpiSegment::write(cmd), SpiSegment::write(data, SpiLanes::Dual)};

    // 1. A driver that reports nothing refuses every device transaction, without suspending
    //    or reaching the hardware; the legacy API keeps working.
    {
        FakeSpiBus legacy({});
        SpiResult r{};
        auto t = one(legacy, kFpga, dual, r);
        t.resume();
        assert(t.done() && r.status == SpiStatus::NotSupported && legacy.starts == 0);
        assert(to_io_status(r.status) == IoStatus::NotSupported);

        std::array<uint8_t, 4> tx{1, 2, 3, 4};
        std::array<uint8_t, 4> rx{};
        auto lt = [](FakeSpiBus& b, std::span<const uint8_t> t2, std::span<uint8_t> r2, SpiResult& o) -> Task<void> {
            o = co_await b.transfer_async(t2, r2);
        }(legacy, tx, rx, r);
        lt.resume();
        pump(legacy);
        assert(lt.done() && r.status == SpiStatus::Ok && rx == tx && legacy.starts == 1);
    }

    // 2. Capability refusals (well-formed requests): NotSupported, hardware untouched.
    {
        SpiCapabilities single_only = full_caps();
        single_only.lanes = spi_lane_bit(SpiLanes::Single);
        FakeSpiBus bus(single_only);
        SpiResult r{};
        auto check = [&](const SpiDevice& dev, std::span<const SpiSegment> segs, SpiStatus want) {
            auto t = one(bus, dev, segs, r);
            t.resume();
            assert(t.done() && r.status == want);
        };
        check(kFpga, dual, SpiStatus::NotSupported);                         // 2 lanes
        const std::array<SpiSegment, 1> plain{SpiSegment::write(cmd)};
        check(SpiDevice{kCsImu, SpiMode::Mode1, 1'000'000}, plain, SpiStatus::NotSupported);   // mode 1
        check(SpiDevice{7, SpiMode::Mode0, 1'000'000}, plain, SpiStatus::NotSupported);        // CS 7
        check(SpiDevice{kCsImu, SpiMode::Mode0, 1'000'000, true}, plain, SpiStatus::NotSupported);  // CS high
        check(SpiDevice{kCsImu, SpiMode::Mode0, 1'000'000, false, true}, plain, SpiStatus::NotSupported); // LSB
        std::array<SpiSegment, 5> many{};
        for (auto& s : many) s = SpiSegment::write(cmd);
        check(kImu, many, SpiStatus::NotSupported);                          // > max_segments
        std::array<SpiSegment, 2> rel{SpiSegment::write(cmd), SpiSegment::write(cmd)};
        rel[0].release_cs_after = true;
        check(kImu, rel, SpiStatus::NotSupported);                           // CS release between

        // 3. Malformed requests: InvalidArgument.
        check(kImu, std::span<const SpiSegment>{}, SpiStatus::InvalidArgument);
        const std::array<SpiSegment, 1> empty{SpiSegment{}};
        check(kImu, empty, SpiStatus::InvalidArgument);
        const std::array<SpiSegment, 1> both_dual{SpiSegment{data, in, SpiLanes::Dual, false}};
        check(kImu, both_dual, SpiStatus::InvalidArgument);
        const std::array<SpiSegment, 1> uneven{SpiSegment::duplex(cmd, in)};
        check(kImu, uneven, SpiStatus::InvalidArgument);
        check(SpiDevice{kCsImu, SpiMode::Mode0, 0}, plain, SpiStatus::InvalidArgument);
        assert(bus.starts == 0);
    }

    // 4. One bus driver, three devices.
    FakeSpiBus bus(full_caps());
    bool ok = false;
    auto f = fpga_roundtrip(bus, 0x10, ok);
    f.resume();
    pump(bus);
    assert(f.done() && ok);
    assert(bus.log.cs == kCsFpga && bus.log.segments == 2 && bus.log.lanes[0] == 1 && bus.log.lanes[1] == 2);
    assert(bus.log.sclk_cycles == 8 + 256);   // ASP_SPI_TRANSPORT.md: 256 Dual-SPI clocks per TLP

    auto fl = flash_reads(bus, 0x012340, ok);
    fl.resume();
    pump(bus);
    assert(fl.done() && ok);
    assert(bus.log.lanes[1] == 4 && bus.log.sclk_cycles == 5 * 8 + 256 * 8 / 4);

    auto im = imu_regs(bus, ok);
    im.resume();
    pump(bus);
    assert(im.done() && ok && bus.log.cs == kCsImu);

    // 5. Queue full: 16 queued + 1 active; the 18th is refused with QueueFull, not lost.
    {
        FakeSpiBus q(full_caps());
        const std::array<SpiSegment, 1> plain{SpiSegment::write(cmd)};
        std::array<SpiResult, 18> res{};
        std::array<std::optional<Task<void>>, 18> tasks{};
        for (size_t i = 0; i < tasks.size(); ++i) {
            tasks[i].emplace(one(q, kImu, plain, res[i]));
            tasks[i]->resume();
        }
        assert(tasks[17]->done() && res[17].status == SpiStatus::QueueFull);
        pump(q);
        for (size_t i = 0; i < 17; ++i) assert(tasks[i]->done() && res[i].status == SpiStatus::Ok);
    }

    // 6. Steady state: 1000 rounds of FPGA + flash + IMU transactions on one driver.
    {
        {   // warm-up: one frame of each size is in the pool before the high-water mark is taken
            auto w1 = fpga_roundtrip(bus, 0, ok);
            auto w2 = flash_reads(bus, 0, ok);
            auto w3 = imu_regs(bus, ok);
            w1.resume();
            pump(bus);
            w2.resume();
            pump(bus);
            w3.resume();
            pump(bus);
        }
        const size_t hw0 = coro_pool_high_water();
        const uint32_t dropped0 = Dispatcher::dropped_resumes();
        for (int i = 0; i < 1000; ++i) {
            auto a = fpga_roundtrip(bus, static_cast<uint8_t>(i), ok);
            a.resume();
            pump(bus);
            assert(a.done() && ok);
            auto b = flash_reads(bus, static_cast<uint32_t>(i) * 256U, ok);
            b.resume();
            pump(bus);
            assert(b.done() && ok);
            auto c = imu_regs(bus, ok);
            c.resume();
            pump(bus);
            assert(c.done() && ok);
        }
        assert(coro_pool_high_water() == hw0);
        assert(Dispatcher::dropped_resumes() == dropped0);
        assert(!bus.has_pending_requests() && !bus.has_active() && Dispatcher::queue_depth() == 0);
    }

    std::printf("[PASS] SPI transactions: IMU, FPGA Dual-SPI and flash on one bus driver; refusals never reach hardware; 1000 rounds flat\n");
    return 0;
}
