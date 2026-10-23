/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: UART configuration by capability [SPEC-HAL-06]
 * A request outside a driver's capabilities is refused with NotSupported and never reaches the
 * hardware; a driver that reports nothing accepts only 8N1 without gap; a capable driver gets the
 * whole config.
 */
// @verifies [SPEC-HAL-06] docs/DESIGN_SPECIFICATION.md
#undef NDEBUG

#include "abstractx/hal/uart.hpp"

#include <cassert>
#include <cstdio>

using namespace abstractx::hal;

namespace {

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
    bool set_baud_rate(uint32_t b) override { baud = b; return true; }
    UartCapabilities capabilities() const noexcept override { return caps_; }

    int applied{0};
    uint32_t baud{0};
    UartConfig last{};

protected:
    UartStatus apply_config(const UartConfig& cfg) noexcept override {
        ++applied;
        last = cfg;
        baud = cfg.baud;
        return UartStatus::Ok;
    }
    void start_hardware_transfer_from_isr(const UartTxRequest&) noexcept override {}

private:
    UartCapabilities caps_;
};

} // namespace

int main() {
    // 1. A driver reporting nothing: plain 8N1 at its baud only.
    FakeUart plain({});
    assert(plain.configure(UartConfig{}) == UartStatus::Ok && plain.applied == 1);
    UartConfig two_stop{};
    two_stop.stop_bits = 2;
    UartConfig gap{};
    gap.tx_gap_us = 10;
    UartConfig even{};
    even.parity = UartParity::Even;
    UartConfig rts{};
    rts.flow = UartFlow::RtsCts;
    for (const auto& c : {two_stop, gap, even, rts}) {
        assert(plain.configure(c) == UartStatus::NotSupported);
    }
    assert(plain.applied == 1);  // refused requests never reach the hardware

    // 2. T527-like UART: 1.5 Mbit/s at 24 MHz, 128 B FIFO, 2 stop bits, parity, gap up to 170 us.
    UartCapabilities t527{};
    t527.max_baud = 1'500'000;
    t527.fifo_bytes = 128;
    t527.min_data_bits = 5;
    t527.max_stop_bits = 2;
    t527.parity = true;
    t527.max_tx_gap_us = 170;
    t527.tx_gap_step_ns = 667;
    FakeUart e906(t527);
    UartConfig dev{};
    dev.baud = 1'000'000;
    dev.stop_bits = 2;
    dev.parity = UartParity::Odd;
    dev.tx_gap_us = 20;
    assert(e906.configure(dev) == UartStatus::Ok && e906.last.tx_gap_us == 20 && e906.baud == 1'000'000);
    UartConfig fast = dev;
    fast.baud = 5'000'000;                        // needs APB1 >= 80 MHz
    assert(e906.configure(fast) == UartStatus::NotSupported);
    UartConfig long_gap = dev;
    long_gap.tx_gap_us = 500;                     // beyond TX_DELAY's range
    assert(e906.configure(long_gap) == UartStatus::NotSupported);
    UartConfig flow = dev;
    flow.flow = UartFlow::RtsCts;                 // not reported: refused
    assert(e906.configure(flow) == UartStatus::NotSupported);
    assert(e906.applied == 1);

    // 3. The check is usable at compile time.
    static_assert(check_uart_config(UartCapabilities{}, UartConfig{}) == UartStatus::Ok);
    static_assert(check_uart_config(UartCapabilities{}, UartConfig{9600, 7}) == UartStatus::NotSupported);

    std::printf("[PASS] UART configure by capability: refused requests never applied\n");
    return 0;
}
