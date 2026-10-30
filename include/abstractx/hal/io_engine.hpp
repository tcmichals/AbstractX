/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX HAL: shared I/O processor engine ([SPEC-ARCH-13]).
 * IoEngine decodes egress TLPs, runs auto channels and bridge transfers, and
 * frames completions. A target only provides an IIoBackend (bus, trigger, doorbell).
 */

#ifndef ABSTRACTX_HAL_IO_ENGINE_HPP
#define ABSTRACTX_HAL_IO_ENGINE_HPP

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include <etl/delegate.h>

#include "abstractx/hal/io_processor.hpp"
#include "abstractx/interrupt_lock.hpp"
#include "asp_tlp_msg.hpp"
#include "pcie_bar_map.hpp"

namespace abstractx::hal {

// Outcome of one bus transfer, reported by the back end.
struct IoResult {
    bool     ok{false};
    size_t   transferred{0};
    uint32_t duration_us{0};
    uint64_t timestamp_ns{0};
};

// One bus transfer the engine asks a back end to run. The spans point into engine-owned
// storage that stays valid until `done` is called ([AGENTS §10.3]).
struct IoTransfer {
    BusType  bus{BusType::Spi};
    uint8_t  bus_index{0};
    uint8_t  device{0};                 // SPI chip select / I2C 7-bit address
    std::span<const uint8_t> tx{};      // SPI: same length as rx
    std::span<uint8_t>       rx{};
    etl::delegate<void(const IoResult&)> done{};
};

// The only target-specific part of a software I/O processor ([SPEC-ARCH-13] 2).
class IIoBackend {
public:
    virtual ~IIoBackend() = default;

    // Set up the bus and arm the trigger of one configured channel.
    virtual bool init_channel(const AutoChannelConfig& ch) noexcept = 0;

    // Start a transfer. true: `done` is called exactly once, later or before returning.
    // false: not started, `done` is never called.
    virtual bool start_transfer(const IoTransfer& xfer) noexcept = 0;

    // GPIO bridge. Returns false for an unsupported command or pin.
    virtual bool gpio(const asp_tlp_gpio_req_header_t& req, uint8_t& level) noexcept {
        (void)req; (void)level;
        return false;
    }

    // Virtual BAR window (LED, peripheral registers).
    virtual uint32_t read_reg(uint32_t addr) noexcept { (void)addr; return 0; }
    virtual void write_reg(uint32_t addr, uint32_t value) noexcept { (void)addr; (void)value; }

    // Wake the consumer of the ingress ring (MSGBOX, SIO FIFO, eventfd).
    virtual void notify_host() noexcept {}

    // Service target events (e.g. Linux epoll triggers); returns the number handled.
    virtual int poll_events(int timeout_ms) noexcept { (void)timeout_ms; return 0; }

    // Sleep until the next interrupt (run() only).
    virtual void idle_wait() noexcept {}

    virtual uint64_t now_ns() noexcept { return 0; }

    virtual void on_stop() noexcept {}
};

// @impl [SPEC-ARCH-13] docs/DESIGN_SPECIFICATION.md
class IoEngine : public IIoProcessor {
public:
    static constexpr size_t kMaxChannels    = 4;
    static constexpr size_t kBridgeSlots    = 8;
    static constexpr size_t kAutoCmdMax     = 8;
    static constexpr size_t kAutoXferMax    = 64;
    static constexpr size_t kSpiBridgeMax   = 36;
    static constexpr size_t kI2cTxMax       = 32;
    static constexpr size_t kI2cRxMax       = 36;

    explicit IoEngine(IIoBackend& backend) noexcept : backend_(backend) {
        for (size_t i = 0; i < kMaxChannels; ++i) { autos_[i].owner = this; autos_[i].index = i; }
        for (auto& s : slots_) { s.owner = this; }
    }

    bool configure(const IoProcessorSetup& setup) override {
        setup_ = setup;
        InterruptGuard guard;
        num_channels_ = 0;
        for (const auto& ch : setup_.channels) {
            if (num_channels_ >= kMaxChannels) break;
            autos_[num_channels_].cfg = ch;
            autos_[num_channels_].busy.store(false, std::memory_order_relaxed);
            (void)backend_.init_channel(ch);
            ++num_channels_;
        }
        configured_ = true;
        return true;
    }

    bool start() override {
        if (!configured_) return false;
        running_.store(true, std::memory_order_release);
        return true;
    }

    void stop() override {
        if (running_.exchange(false, std::memory_order_acq_rel)) {
            backend_.on_stop();
        }
    }

    int step(int timeout_ms = 0) override {
        drain_egress();
        return backend_.poll_events(timeout_ms);
    }

    void run() override {
        running_.store(true, std::memory_order_release);
        while (running_.load(std::memory_order_acquire)) {
            drain_egress();
            if (backend_.poll_events(0) == 0) backend_.idle_wait();
        }
    }

    bool is_running() const noexcept override { return running_.load(std::memory_order_acquire); }

    bool set_auto_mode(uint8_t channel_id, bool enable) override {
        InterruptGuard guard;
        for (size_t i = 0; i < num_channels_; ++i) {
            if (autos_[i].cfg.channel_id == channel_id) {
                autos_[i].cfg.auto_mode = enable;
                return true;
            }
        }
        return false;
    }

    // Trigger edge on `pin` (ISR, GPIO event or timer). Starts each armed channel on that pin.
    // @impl [SPEC-ARCH-13] docs/DESIGN_SPECIFICATION.md
    void on_trigger(uint32_t pin, uint64_t ts_ns) noexcept {
        for (size_t i = 0; i < num_channels_; ++i) {
            AutoState& a = autos_[i];
            {
                InterruptGuard guard;
                if (!a.cfg.auto_mode || a.cfg.trigger_pin != pin) continue;
                if (a.busy.exchange(true, std::memory_order_acq_rel)) {
                    overruns_.fetch_add(1, std::memory_order_relaxed);   // still in flight: drop
                    continue;
                }
                a.snap = a.cfg;
            }
            a.ts_ns = ts_ns;
            if (a.snap.bus_type != BusType::Spi) {     // only SPI auto reads are defined
                a.busy.store(false, std::memory_order_release);
                continue;
            }
            a.cmd_len = a.snap.tx_len > kAutoCmdMax ? kAutoCmdMax : a.snap.tx_len;
            size_t xfer = a.cmd_len + a.snap.rx_len;
            if (xfer > kAutoXferMax) xfer = kAutoXferMax;
            std::memset(a.tx, 0, sizeof(a.tx));
            std::memcpy(a.tx, a.snap.tx_cmd, a.cmd_len);

            IoTransfer t{};
            t.bus = BusType::Spi;
            t.bus_index = a.snap.bus_index;
            t.device = static_cast<uint8_t>(a.snap.cs_pin);
            t.tx = std::span<const uint8_t>(a.tx, xfer);
            t.rx = std::span<uint8_t>(a.rx, xfer);
            t.done = etl::delegate<void(const IoResult&)>::create<AutoState, &AutoState::on_done>(a);
            if (!backend_.start_transfer(t)) {
                a.busy.store(false, std::memory_order_release);
            }
        }
    }

    uint32_t auto_overruns() const noexcept { return overruns_.load(std::memory_order_relaxed); }
    uint32_t ingress_dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }
    size_t bridge_slots_in_use() const noexcept {
        size_t n = 0;
        for (const auto& s : slots_) n += s.in_use.load(std::memory_order_acquire) ? 1U : 0U;
        return n;
    }

private:
    struct AutoState {
        IoEngine* owner{nullptr};
        size_t index{0};
        AutoChannelConfig cfg{};       // written under InterruptGuard
        AutoChannelConfig snap{};      // owned by the in-flight transfer
        std::atomic<bool> busy{false};
        uint64_t ts_ns{0};
        size_t cmd_len{0};
        alignas(4) uint8_t tx[kAutoXferMax]{};
        alignas(4) uint8_t rx[kAutoXferMax]{};

        void on_done(const IoResult& r) noexcept { owner->auto_done(*this, r); }
    };

    struct BridgeSlot {
        IoEngine* owner{nullptr};
        std::atomic<bool> in_use{false};
        BusType bus{BusType::Spi};
        uint8_t tag{0};
        Channel channel{Channel::Control};
        size_t rx_len{0};
        alignas(4) uint8_t tx[kSpiBridgeMax]{};
        alignas(4) uint8_t rx[kSpiBridgeMax]{};

        void on_done(const IoResult& r) noexcept { owner->bridge_done(*this, r); }
    };

    void emit(const Tlp64& tlp) noexcept {
        if (!setup_.ingress_rx_ring) return;
        if (!setup_.ingress_rx_ring->push(tlp)) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        backend_.notify_host();
        if (setup_.on_rx_pushed.is_valid()) setup_.on_rx_pushed();
    }

    // Copy, release, then publish ([SPEC-ARCH-13] 4): the consumer may react on another thread.
    void auto_done(AutoState& a, const IoResult& r) noexcept {
        if (!r.ok) {
            a.busy.store(false, std::memory_order_release);
            return;
        }
        const size_t got = r.transferred < kAutoXferMax ? r.transferred : kAutoXferMax;
        const size_t payload = got > a.cmd_len ? got - a.cmd_len : 0;
        Tlp64 tlp = Tlp64::make_stream_tx(static_cast<Channel>(a.snap.tlp_channel), 0,
                                          std::span<const uint8_t>(a.rx + a.cmd_len, payload),
                                          a.snap.tlp_tag);
        tlp.wire.timestamp_ns = a.ts_ns;
        a.busy.store(false, std::memory_order_release);   // a.rx may be reused from here on
        emit(tlp);
    }

    BridgeSlot* alloc_slot() noexcept {
        for (auto& s : slots_) {
            bool expected = false;
            if (s.in_use.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return &s;
        }
        return nullptr;
    }

    void bridge_done(BridgeSlot& s, const IoResult& r) noexcept {
        const uint8_t status = r.ok ? ASP_STATUS_OK : ASP_STATUS_DMA_ERROR;
        size_t n = r.ok ? r.transferred : 0;
        if (n > s.rx_len) n = s.rx_len;
        const auto data = std::span<const uint8_t>(s.rx, n);
        const uint16_t dur = static_cast<uint16_t>(r.duration_us > 0xFFFFU ? 0xFFFFU : r.duration_us);
        emit(s.bus == BusType::Spi
                 ? Tlp64::make_spi_cpl(s.tag, status, data, dur, r.timestamp_ns, s.channel)
                 : Tlp64::make_i2c_cpl(s.tag, status, data, dur, r.timestamp_ns, s.channel));
        s.in_use.store(false, std::memory_order_release);
    }

    bool bus_locked(BusType bus) const noexcept {
        InterruptGuard guard;
        for (size_t i = 0; i < num_channels_; ++i) {
            if (autos_[i].cfg.bus_type == bus && autos_[i].cfg.auto_mode) return true;
        }
        return false;
    }

    void reject(const Tlp64& tlp, BusType bus, uint8_t status) noexcept {
        emit(bus == BusType::Spi ? Tlp64::make_spi_cpl(tlp.tag(), status, {}, 0, 0, tlp.channel())
                                 : Tlp64::make_i2c_cpl(tlp.tag(), status, {}, 0, 0, tlp.channel()));
    }

    void handle_dma_cfg(const Tlp64& tlp) noexcept {
        asp_tlp_auto_dma_cfg_t cfg{};
        std::memcpy(&cfg, tlp.wire.payload, sizeof(cfg));
        uint8_t status = ASP_STATUS_OK;
        if (cfg.cmd == ASP_DMA_CMD_ENABLE || cfg.cmd == ASP_DMA_CMD_DISABLE) {
            if (!set_auto_mode(cfg.channel_id, cfg.cmd == ASP_DMA_CMD_ENABLE)) status = ASP_STATUS_DMA_ERROR;
        } else if (cfg.cmd == ASP_DMA_CMD_SETUP && cfg.channel_id < num_channels_) {
            InterruptGuard guard;
            auto& ch = autos_[cfg.channel_id].cfg;
            ch.trigger_pin = cfg.trigger_pin;
            ch.trigger_rising = (cfg.trigger_edge == ASP_GPIO_EDGE_RISING || cfg.trigger_edge == ASP_GPIO_EDGE_BOTH);
            ch.bus_type = static_cast<BusType>(cfg.bus_type);
            ch.bus_index = cfg.bus_id;
            ch.tx_len = cfg.tx_len > kAutoCmdMax ? static_cast<uint8_t>(kAutoCmdMax) : cfg.tx_len;
            ch.rx_len = cfg.rx_len;
            std::memcpy(ch.tx_cmd, cfg.tx_cmd, sizeof(ch.tx_cmd));
            ch.tlp_channel = cfg.tlp_channel;
            ch.tlp_tag = cfg.tlp_tag;
        } else {
            status = ASP_STATUS_DMA_ERROR;
        }
        emit(Tlp64::make_hw_fusion_cpl(tlp.tag(), status, cfg.cmd, cfg.channel_id));
    }

    void handle_spi(const Tlp64& tlp) noexcept {
        if (bus_locked(BusType::Spi)) { reject(tlp, BusType::Spi, ASP_STATUS_BUS_LOCKED); return; }
        TlpSpiView view(tlp);
        const auto* hdr = view.req_header();
        const auto tx = view.tx_data();                         // clamped to 36 by the view
        size_t rx_len = hdr->rx_len > kSpiBridgeMax ? kSpiBridgeMax : hdr->rx_len;
        const size_t xfer = tx.size() > rx_len ? tx.size() : rx_len;
        BridgeSlot* s = alloc_slot();
        if (!s) { reject(tlp, BusType::Spi, ASP_STATUS_QUEUE_FULL); return; }
        s->bus = BusType::Spi;
        s->tag = tlp.tag();
        s->channel = tlp.channel();
        s->rx_len = xfer;                                       // SPI clocks xfer bytes in
        std::memset(s->tx, 0, sizeof(s->tx));
        std::memcpy(s->tx, tx.data(), tx.size());
        IoTransfer t{};
        t.bus = BusType::Spi;
        t.bus_index = hdr->bus_id;
        t.device = hdr->cs_pin;
        t.tx = std::span<const uint8_t>(s->tx, xfer);
        t.rx = std::span<uint8_t>(s->rx, xfer);
        t.done = etl::delegate<void(const IoResult&)>::create<BridgeSlot, &BridgeSlot::on_done>(*s);
        if (!backend_.start_transfer(t)) {
            s->in_use.store(false, std::memory_order_release);
            reject(tlp, BusType::Spi, ASP_STATUS_QUEUE_FULL);
        }
    }

    void handle_i2c(const Tlp64& tlp) noexcept {
        if (bus_locked(BusType::I2c)) { reject(tlp, BusType::I2c, ASP_STATUS_BUS_LOCKED); return; }
        TlpI2cView view(tlp);
        const auto* hdr = view.req_header();
        BridgeSlot* s = alloc_slot();
        if (!s) { reject(tlp, BusType::I2c, ASP_STATUS_QUEUE_FULL); return; }
        s->bus = BusType::I2c;
        s->tag = tlp.tag();
        s->channel = tlp.channel();
        s->rx_len = hdr->rx_len > kI2cRxMax ? kI2cRxMax : hdr->rx_len;
        size_t tx_n = 0;
        if (hdr->sub_flags & ASP_I2C_FLAG_USE_REG) {
            s->tx[0] = hdr->reg_offset;
            tx_n = 1;
        } else {
            const auto tx = view.tx_data();                     // clamped to 32 by the view
            std::memcpy(s->tx, tx.data(), tx.size());
            tx_n = tx.size();
        }
        IoTransfer t{};
        t.bus = BusType::I2c;
        t.device = hdr->slave_addr;
        t.tx = std::span<const uint8_t>(s->tx, tx_n);
        t.rx = std::span<uint8_t>(s->rx, s->rx_len);
        t.done = etl::delegate<void(const IoResult&)>::create<BridgeSlot, &BridgeSlot::on_done>(*s);
        if (!backend_.start_transfer(t)) {
            s->in_use.store(false, std::memory_order_release);
            reject(tlp, BusType::I2c, ASP_STATUS_QUEUE_FULL);
        }
    }

    void handle_gpio(const Tlp64& tlp) noexcept {
        asp_tlp_gpio_req_header_t req{};
        std::memcpy(&req, tlp.wire.payload, sizeof(req));
        uint8_t level = 0;
        const bool ok = req.pin < 32 && backend_.gpio(req, level);
        if (req.cmd == ASP_GPIO_CMD_READ || !ok) {
            emit(Tlp64::make_gpio_cpl(tlp.tag(), ok ? ASP_STATUS_OK : ASP_STATUS_DMA_ERROR, req.pin, level));
        }
    }

    static constexpr bool in_window(uint32_t addr, uint32_t base, uint32_t size) noexcept {
        return addr >= base && addr - base < size;
    }

    // Bridge requests are MemRead/MemWrite TLPs aimed at a bridge window, so the window (or a
    // bridge channel) is checked before the generic virtual-BAR MemRead/MemWrite path.
    void drain_egress() noexcept {
        if (!setup_.egress_tx_ring) return;
        Tlp64 tlp{};
        while (setup_.egress_tx_ring->pop(tlp)) {
            if (tlp.wire.type == ASP_TLP_TYPE_DMA_CFG) { handle_dma_cfg(tlp); continue; }
            const auto ch = static_cast<uint8_t>(tlp.channel());
            const uint32_t addr = tlp.wire.target_address;
            if (ch == ASP_CHANNEL_SPI_BRIDGE || in_window(addr, ASP_ADDR_SPI_BASE, 0x80U)) {
                handle_spi(tlp);
                continue;
            }
            if (ch == ASP_CHANNEL_I2C_BRIDGE || in_window(addr, ASP_ADDR_I2C_BASE, 0x80U)) {
                handle_i2c(tlp);
                continue;
            }
            if (ch == ASP_CHANNEL_GPIO_BRIDGE || in_window(addr, ASP_ADDR_GPIO_BASE, 0x400U)) {
                handle_gpio(tlp);
                continue;
            }
            if (tlp.wire.type == ASP_TLP_TYPE_MEM_WR) {
                const uint32_t v = (static_cast<uint32_t>(tlp.wire.payload[0]) << 24) |
                                   (static_cast<uint32_t>(tlp.wire.payload[1]) << 16) |
                                   (static_cast<uint32_t>(tlp.wire.payload[2]) << 8) |
                                   static_cast<uint32_t>(tlp.wire.payload[3]);
                backend_.write_reg(addr, v);
                continue;
            }
            if (tlp.wire.type == ASP_TLP_TYPE_MEM_RD) {
                emit(Tlp64::make_completion_data(tlp.tag(), backend_.read_reg(addr), tlp.channel()));
            }
        }
    }

    IIoBackend& backend_;
    IoProcessorSetup setup_{};
    bool configured_{false};
    std::atomic<bool> running_{false};
    size_t num_channels_{0};
    std::array<AutoState, kMaxChannels> autos_{};
    std::array<BridgeSlot, kBridgeSlots> slots_{};
    std::atomic<uint32_t> overruns_{0};
    std::atomic<uint32_t> dropped_{0};
};

} // namespace abstractx::hal

#endif // ABSTRACTX_HAL_IO_ENGINE_HPP
