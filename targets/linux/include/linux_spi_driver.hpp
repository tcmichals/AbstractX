/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Linux HAL: spidev SPI driver (ISpi) over SpiWorker.
 */

#ifndef ABSTRACTX_TARGET_LINUX_SPI_DRIVER_HPP
#define ABSTRACTX_TARGET_LINUX_SPI_DRIVER_HPP

#include "abstractx/hal/spi.hpp"
#include "spi_worker.hpp"

namespace abstractx::hal {

// @impl [SPEC-HAL-02] targets/SPECIFICATION.md#42-spi-interface-ispi--asyncspidriver
// @impl [SPEC-LINUX-HAL-01] targets/linux/SPECIFICATION.md
class LinuxSpiDriver : public ISpi {
public:
    explicit LinuxSpiDriver(const char* spidev_path = "/dev/spidev1.0") noexcept
        : spidev_path_(spidev_path) {
        worker_.set_completion_sink(
            target::SpiWorker::CompletionSink::create<LinuxSpiDriver, &LinuxSpiDriver::on_worker_done>(*this));
    }
    ~LinuxSpiDriver() override {
        worker_.stop();
    }

    bool init(const SpiConfig& config) override {
        config_ = config;
        uint8_t mode = static_cast<uint8_t>(config.mode);
        return worker_.start(spidev_path_, config.frequency_hz, mode);
    }

    void shutdown() noexcept { worker_.stop(); }

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
    // @impl [SPEC-LINUX-HAL-01] targets/linux/SPECIFICATION.md
    void start_hardware_transfer_from_isr(const SpiRequest& req) noexcept override {
        if (!worker_.submit(req)) {
            SpiResult refused{};
            refused.status = SpiStatus::QueueFull;
            push_completion_from_isr(req, refused);
        }
        set_hardware_idle_from_isr();
    }

private:
    // Worker thread: deliver the result to the awaiter's result_slot, then post its handle.
    void on_worker_done(const SpiRequest& req, const SpiResult& res) noexcept {
        push_completion_from_isr(req, res);
    }

private:
    const char*         spidev_path_;
    SpiConfig           config_{};
    target::SpiWorker   worker_{};
};

} // namespace abstractx::hal

#endif // ABSTRACTX_TARGET_LINUX_SPI_DRIVER_HPP
