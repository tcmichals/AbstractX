/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: shared I/O processor engine with a mock back end [SPEC-ARCH-13].
 * Host-supplied lengths are clamped, a trigger only starts its own channel, a trigger while
 * the channel is busy is counted and dropped, bus lockout, bridge slot exhaustion, GPIO pin
 * bounds, and 1000 rounds with no slot or ring growth.
 */
// @verifies [SPEC-ARCH-13] docs/DESIGN_SPECIFICATION.md
// @verifies [SPEC-APP-11] apps/gps_imu_app/SPECIFICATION.md
#undef NDEBUG

#include "abstractx/hal/io_engine.hpp"

#include <cassert>
#include <cstdio>

using namespace abstractx;
using namespace abstractx::hal;

namespace {

// Holds started transfers until the test completes them, like a DMA in flight.
struct MockBackend final : IIoBackend {
    static constexpr size_t kMax = 16;
    IoTransfer pending[kMax]{};
    size_t n_pending{0};
    size_t started{0};
    size_t last_tx{0}, last_rx{0};
    size_t init_calls{0};
    uint32_t last_init_pin{0};
    bool refuse_init{false};
    bool refuse{false};
    bool refuse_if_busy{false};
    uint32_t notified{0};

    bool init_channel(const AutoChannelConfig& ch) noexcept override {
        if (refuse_init) return false;
        ++init_calls;
        last_init_pin = ch.trigger_pin;
        return true;
    }
    bool start_transfer(const IoTransfer& x) noexcept override {
        if (refuse || n_pending >= kMax) return false;
        if (refuse_if_busy) {
            for (size_t i = 0; i < n_pending; ++i) {
                if (pending[i].bus == x.bus && pending[i].bus_index == x.bus_index) {
                    return false; // Hardware bus is busy with another transfer
                }
            }
        }
        assert(x.bus != BusType::Spi || x.tx.size() == x.rx.size());
        last_tx = x.tx.size();
        last_rx = x.rx.size();
        pending[n_pending++] = x;
        ++started;
        return true;
    }
    uint32_t last_baud{0};
    size_t last_serial_tx{0};

    bool gpio(const asp_tlp_gpio_req_header_t& req, uint8_t& level) noexcept override {
        level = static_cast<uint8_t>(req.pin & 1U);
        return true;
    }
    size_t serial_xfer(uint8_t /*port*/, uint32_t baud_hz,
                       std::span<const uint8_t> tx,
                       std::span<uint8_t> rx) noexcept override {
        if (baud_hz != 0) last_baud = baud_hz;
        last_serial_tx = tx.size();
        const char nmea[] = "$GPGGA,00*66\n";
        const size_t n = sizeof(nmea) - 1 < rx.size() ? sizeof(nmea) - 1 : rx.size();
        std::memcpy(rx.data(), nmea, n);
        return n;
    }
    void notify_host() noexcept override { ++notified; }

    // Complete the oldest pending transfer in FIFO order
    void complete_one(bool ok = true) {
        if (n_pending == 0) return;
        IoTransfer x = pending[0];
        for (size_t i = 1; i < n_pending; ++i) pending[i - 1] = pending[i];
        --n_pending;
        for (size_t i = 0; i < x.rx.size(); ++i) x.rx[i] = static_cast<uint8_t>(0xA0U + i);
        IoResult r{};
        r.ok = ok;
        r.transferred = x.rx.size();
        x.done(r);
    }

    // Complete every pending transfer: fill rx with a pattern (ASan sees any overrun).
    void complete_all(bool ok = true) {
        while (n_pending > 0) {
            IoTransfer x = pending[--n_pending];
            for (size_t i = 0; i < x.rx.size(); ++i) x.rx[i] = static_cast<uint8_t>(0xA0U + i);
            IoResult r{};
            r.ok = ok;
            r.transferred = x.rx.size();
            x.done(r);
        }
    }
};

Tlp64 dma_cfg(uint8_t cmd, uint8_t ch, uint8_t pin, uint8_t tx_len, uint8_t rx_len, uint8_t tag) {
    Tlp64 t{};
    t.wire.type = ASP_TLP_TYPE_DMA_CFG;
    t.wire.tag = tag;
    asp_tlp_auto_dma_cfg_t c{};
    c.cmd = cmd;
    c.channel_id = ch;
    c.trigger_pin = pin;
    c.trigger_edge = ASP_GPIO_EDGE_RISING;
    c.bus_type = 0;
    c.tx_len = tx_len;
    c.rx_len = rx_len;
    c.tlp_channel = ASP_CHANNEL_TELEMETRY;
    std::memcpy(t.wire.payload, &c, sizeof(c));
    return t;
}

size_t drain(MpscTlpRing<>& ring) {
    size_t n = 0;
    Tlp64 t{};
    while (ring.pop(t)) ++n;
    return n;
}

} // namespace

int main() {
    MockBackend be;
    IoEngine eng(be);
    MpscTlpRing<> to_io{}, from_io{};

    AutoChannelConfig ch[2]{};
    ch[0].channel_id = 0; ch[0].trigger_pin = 20; ch[0].auto_mode = false;
    ch[0].tx_cmd[0] = 0x9F; ch[0].tx_len = 1; ch[0].rx_len = 15;
    ch[1].channel_id = 1; ch[1].trigger_pin = 21; ch[1].auto_mode = false;
    ch[1].tx_len = 1; ch[1].rx_len = 6;
    IoProcessorSetup setup{};
    setup.channels = std::span<const AutoChannelConfig>(ch, 2);
    setup.egress_tx_ring = &to_io;
    setup.ingress_rx_ring = &from_io;
    assert(eng.init(setup));

    // 1. Host-controlled lengths (tx 255, rx 255) are clamped: 8-byte command, 64-byte transfer.
    //    DMA_CFG SETUP also re-arms the back end via init_channel().
    const size_t init_before = be.init_calls;
    assert(to_io.push(dma_cfg(ASP_DMA_CMD_SETUP, 0, 22, 255, 255, 1)));
    assert(to_io.push(dma_cfg(ASP_DMA_CMD_ENABLE, 0, 0, 0, 0, 2)));
    eng.step(0);
    assert(be.init_calls == init_before + 1 && be.last_init_pin == 22);
    assert(drain(from_io) == 2);
    eng.on_trigger(22, 123);
    assert(be.started == 1 && be.last_tx == IoEngine::kAutoXferMax && be.last_rx == IoEngine::kAutoXferMax);

    // 1b. DMA_CFG SETUP while the channel is in flight is refused with ASP_STATUS_DMA_ERROR.
    assert(to_io.push(dma_cfg(ASP_DMA_CMD_SETUP, 0, 20, 1, 15, 10)));
    eng.step(0);
    Tlp64 cfg_reply{};
    assert(from_io.pop(cfg_reply) && cfg_reply.tag() == 10 && cfg_reply.wire.payload[0] == ASP_STATUS_DMA_ERROR);

    // 2. A second edge while the transfer is in flight is counted and dropped.
    eng.on_trigger(22, 124);
    assert(be.started == 1 && eng.auto_overruns() == 1);

    // 3. A trigger on another pin does not start channel 0; channel 1 is not armed.
    eng.on_trigger(21, 125);
    assert(be.started == 1);

    be.complete_all();
    Tlp64 s{};
    assert(from_io.pop(s) && s.wire.timestamp_ns == 123);
    assert(s.wire.payload[0] == 0xA8);                       // payload starts after the 8-byte command
    assert(!from_io.pop(s));

    // 4. Bus lockout: a manual SPI read while channel 0 is in auto mode is rejected.
    uint8_t txb[2] = {0xF5, 0x00};
    assert(to_io.push(Tlp64::make_spi_transfer(0, 0, txb, 2, ASP_SPI_FLAG_AUTO_CS, 42, Channel::Control)));
    eng.step(0);
    assert(from_io.pop(s) && s.tag() == 42 && s.wire.payload[0] == ASP_STATUS_BUS_LOCKED);

    // 5. Bridge slot pool: requests beyond the pool answer QUEUE_FULL; completions free slots.
    assert(to_io.push(dma_cfg(ASP_DMA_CMD_DISABLE, 0, 0, 0, 0, 3)));
    eng.step(0);
    assert(drain(from_io) == 1);
    for (uint8_t i = 0; i < IoEngine::kBridgeSlots + 2; ++i) {
        assert(to_io.push(Tlp64::make_spi_transfer(0, 0, txb, 2, ASP_SPI_FLAG_AUTO_CS, static_cast<uint8_t>(50 + i), Channel::Control)));
    }
    eng.step(0);
    assert(eng.bridge_slots_in_use() == IoEngine::kBridgeSlots);
    size_t full = 0;
    while (from_io.pop(s)) full += (s.wire.payload[0] == ASP_STATUS_QUEUE_FULL) ? 1U : 0U;
    assert(full == 2);
    be.complete_all();
    assert(eng.bridge_slots_in_use() == 0 && drain(from_io) == IoEngine::kBridgeSlots);

    // 6. GPIO pin >= 32 is an error status, never a shift.
    Tlp64 g{};
    g.wire.type = ASP_TLP_TYPE_MEM_WR + 1;   // any non-DMA_CFG/MEM type routes by channel
    g.wire.channel = ASP_CHANNEL_GPIO_BRIDGE;
    asp_tlp_gpio_req_header_t gr{};
    gr.cmd = ASP_GPIO_CMD_SET;
    gr.pin = 40;
    std::memcpy(g.wire.payload, &gr, sizeof(gr));
    assert(to_io.push(g));
    eng.step(0);
    assert(from_io.pop(s) && s.wire.payload[0] != ASP_STATUS_OK);

    // 7. Steady state: 1000 trigger/complete rounds and 1000 bridge rounds; nothing grows.
    assert(to_io.push(dma_cfg(ASP_DMA_CMD_ENABLE, 1, 0, 0, 0, 4)));
    eng.step(0);
    drain(from_io);
    const uint32_t over0 = eng.auto_overruns();
    for (int i = 0; i < 1000; ++i) {
        eng.on_trigger(21, static_cast<uint64_t>(i));
        be.complete_all();
        assert(from_io.pop(s) && s.wire.timestamp_ns == static_cast<uint64_t>(i));
    }
    assert(to_io.push(dma_cfg(ASP_DMA_CMD_DISABLE, 1, 0, 0, 0, 5)));
    eng.step(0);
    drain(from_io);
    for (int i = 0; i < 1000; ++i) {
        assert(to_io.push(Tlp64::make_spi_transfer(0, 0, txb, 2, ASP_SPI_FLAG_AUTO_CS, 7, Channel::Control)));
        eng.step(0);
        be.complete_all();
        assert(from_io.pop(s) && s.tag() == 7 && s.wire.payload[0] == ASP_STATUS_OK);
    }
    assert(eng.auto_overruns() == over0 && eng.bridge_slots_in_use() == 0);
    assert(eng.ingress_dropped() == 0 && from_io.empty());

    // 7b. Bridge requests are MemRead/MemWrite TLPs on the Control channel: they must reach the
    //     I2C and GPIO bridges, not the virtual-BAR path (the E906/Pico decoders answered none).
    assert(to_io.push(Tlp64::make_i2c_read_reg(1, 0x1E, 0x0A, 3, 60)));
    eng.step(0);
    assert(be.n_pending == 1 && be.pending[0].bus == BusType::I2c && be.pending[0].device == 0x1E);
    assert(be.pending[0].tx.size() == 1 && be.pending[0].tx[0] == 0x0A && be.pending[0].rx.size() == 3);
    be.complete_all();
    assert(from_io.pop(s) && s.tag() == 60 && s.wire.payload[0] == ASP_STATUS_OK);
    assert(to_io.push(Tlp64::make_gpio_read(5, 61)));
    eng.step(0);
    assert(from_io.pop(s) && s.tag() == 61 && s.wire.payload[0] == ASP_STATUS_OK);

    // 7c. Generic Serial/UART bridge (Channel::EscSerial = 0x05) sets baud and returns RX bytes ([SPEC-APP-11]).
    Tlp64 ser_baud{};
    ser_baud.wire.type = ASP_TLP_TYPE_MEM_WR;
    ser_baud.wire.channel = ASP_CHANNEL_ESC_SERIAL;
    ser_baud.wire.tag = 62;
    ser_baud.wire.target_address = 0x40000504U;
    ser_baud.wire.length_dw = 1;
    const uint32_t baud = 115200U;
    std::memcpy(ser_baud.wire.payload, &baud, 4);
    assert(to_io.push(ser_baud));
    eng.step(0);
    assert(be.last_baud == 115200U);
    assert(from_io.pop(s) && s.tag() == 62 && s.wire.length_dw == 13 && std::memcmp(s.wire.payload, "$GPGGA,00*66\n", 13) == 0);

    // 8. A refused start frees the slot and answers QUEUE_FULL.
    be.refuse = true;
    assert(to_io.push(Tlp64::make_spi_transfer(0, 0, txb, 2, ASP_SPI_FLAG_AUTO_CS, 9, Channel::Control)));
    eng.step(0);
    assert(from_io.pop(s) && s.tag() == 9 && s.wire.payload[0] == ASP_STATUS_QUEUE_FULL);
    assert(eng.bridge_slots_in_use() == 0);

    // 9. configure() fails when init_channel() fails.
    be.refuse_init = true;
    assert(!eng.configure(setup));

    // 10. Multi-channel shared bus arbitration (TODO.md line 48 / [SPEC-ARCH-13] 4):
    // Two channels on the same SPI bus (bus 0, cs 0 and cs 1) with colliding edges ->
    // mock backend refuses while a transfer is in flight on that bus.
    // Both transfers execute in sequence, both samples delivered with exact edge timestamps,
    // 1000 rounds flat with 0 overruns and 0 drops.
    be.refuse = false;
    be.refuse_init = false;
    be.refuse_if_busy = true;

    AutoChannelConfig arb_ch[2]{};
    arb_ch[0].channel_id = 0;
    arb_ch[0].trigger_pin = 20;
    arb_ch[0].auto_mode = true;
    arb_ch[0].bus_type = BusType::Spi;
    arb_ch[0].bus_index = 0;
    arb_ch[0].cs_pin = 0;
    arb_ch[0].tx_cmd[0] = 0x9F;
    arb_ch[0].tx_len = 1;
    arb_ch[0].rx_len = 14;
    arb_ch[0].tlp_channel = ASP_CHANNEL_TELEMETRY;
    arb_ch[0].tlp_tag = 0x10;

    arb_ch[1].channel_id = 1;
    arb_ch[1].trigger_pin = 21;
    arb_ch[1].auto_mode = true;
    arb_ch[1].bus_type = BusType::Spi;
    arb_ch[1].bus_index = 0;
    arb_ch[1].cs_pin = 1;
    arb_ch[1].tx_cmd[0] = 0x8F;
    arb_ch[1].tx_len = 1;
    arb_ch[1].rx_len = 6;
    arb_ch[1].tlp_channel = ASP_CHANNEL_TELEMETRY;
    arb_ch[1].tlp_tag = 0x20;

    IoProcessorSetup arb_setup{};
    arb_setup.channels = std::span<const AutoChannelConfig>(arb_ch, 2);
    arb_setup.egress_tx_ring = &to_io;
    arb_setup.ingress_rx_ring = &from_io;
    assert(eng.configure(arb_setup));
    assert(eng.start());
    drain(from_io);

    const uint32_t overruns_before = eng.auto_overruns();
    for (int round = 0; round < 1000; ++round) {
        const uint64_t t0 = static_cast<uint64_t>(round * 1000 + 10);
        const uint64_t t1 = static_cast<uint64_t>(round * 1000 + 20);

        // Edge on pin 20: starts channel 0 on SPI0 CS0
        eng.on_trigger(20, t0);
        assert(be.n_pending == 1);
        assert(be.pending[0].device == 0);

        // Colliding edge on pin 21: channel 1 deferred because SPI0 is in flight
        eng.on_trigger(21, t1);
        assert(be.n_pending == 1); // Channel 1 is pending, not yet on backend

        // Complete channel 0: auto_done retries SPI0 and immediately starts channel 1
        be.complete_one();
        assert(be.n_pending == 1);
        assert(be.pending[0].device == 1); // Channel 1 is now in flight on CS1

        // Verify channel 0 sample was emitted with exact timestamp t0
        assert(from_io.pop(s));
        assert(s.wire.timestamp_ns == t0);
        assert(s.tag() == 0x10);

        // Complete channel 1
        be.complete_one();
        assert(be.n_pending == 0);

        // Verify channel 1 sample was emitted with exact timestamp t1
        assert(from_io.pop(s));
        assert(s.wire.timestamp_ns == t1);
        assert(s.tag() == 0x20);

        assert(from_io.empty());
    }
    assert(eng.auto_overruns() == overruns_before);
    assert(eng.ingress_dropped() == 0);

    // 10b. Superseding test on a pending channel:
    // When channel 1 is already pending, a newer edge updates ts_ns and increments overruns.
    {
        eng.on_trigger(20, 5000); // channel 0 in flight
        assert(be.n_pending == 1);
        eng.on_trigger(21, 6000); // channel 1 pending with ts=6000
        assert(be.n_pending == 1);
        eng.on_trigger(21, 7000); // newer edge on channel 1: supersedes ts to 7000, overruns++
        assert(eng.auto_overruns() == overruns_before + 1);

        be.complete_one();        // channel 0 done -> channel 1 starts
        assert(from_io.pop(s) && s.wire.timestamp_ns == 5000);
        be.complete_one();        // channel 1 done
        assert(from_io.pop(s) && s.wire.timestamp_ns == 7000 && "superseded timestamp delivered");
        assert(from_io.empty());
    }

    eng.stop();
    std::printf("[PASS] IoEngine: length clamps, own-pin triggers, overrun drop, bus lockout, slot pool, GPIO bounds, shared bus arbitration, 1000 rounds flat\n");
    return 0;
}
