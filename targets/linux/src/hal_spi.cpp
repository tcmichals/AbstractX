/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Linux HAL: spidev High-Speed SPI Driver with Dedicated Worker Thread
 */

#include "abstractx/hal/spi.hpp"
#include "abstractx/hal/platform.hpp"
#include "abstractx/trace/tracer.hpp"
#include "spi_worker.hpp"
#include <cstring>
#include <algorithm>

namespace abstractx::hal {

// @impl [SPEC-HAL-02] targets/SPECIFICATION.md#42-spi-interface-ispi--asyncspidriver
class LinuxSpiDriver : public ISpi {
public:
    LinuxSpiDriver() = default;
    ~LinuxSpiDriver() override {
        worker_.stop();
    }

    bool init(const SpiConfig& config) override {
        config_ = config;
        uint8_t mode = static_cast<uint8_t>(config.mode);
        return worker_.start("/dev/spidev1.0", config.frequency_hz, mode);
    }

    bool set_frequency(uint32_t frequency_hz) override {
        config_.frequency_hz = frequency_hz;
        return worker_.set_frequency(frequency_hz);
    }

    void select(bool active) override {
        (void)active;
    }


    target::SpiWorker& worker() noexcept {
        return worker_;
    }

protected:
    void start_hardware_transfer_from_isr(const SpiRequest& req) noexcept override {
        worker_.submit(req);
        set_hardware_idle_from_isr();
    }

private:
    SpiConfig           config_{};
    target::SpiWorker   worker_{};
};

static LinuxSpiDriver g_linux_spi;
ISpi& get_spi_driver() noexcept {
    return g_linux_spi;
}

} // namespace abstractx::hal
