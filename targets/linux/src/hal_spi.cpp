/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Linux HAL: spidev High-Speed SPI Driver with Dedicated Worker Thread
 */

#include "abstractx/hal/spi.hpp"
#include "spi_worker.hpp"
#include <cstring>
#include <algorithm>

namespace abstractx::hal {

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

    uint8_t transfer_byte(uint8_t tx) override {
        uint8_t rx = 0;
        transfer_sync(std::span<const uint8_t>(&tx, 1), std::span<uint8_t>(&rx, 1));
        return rx;
    }

    bool transfer_sync(std::span<const uint8_t> tx_data, std::span<uint8_t> rx_data) override {
        size_t len = std::max(tx_data.size(), rx_data.size());
        if (len == 0) return true;

        if (worker_.is_hardware_active()) {
            struct spi_ioc_transfer tr{};
            tr.tx_buf = reinterpret_cast<uint64_t>(tx_data.data());
            tr.rx_buf = reinterpret_cast<uint64_t>(rx_data.data());
            tr.len = static_cast<uint32_t>(len);
            tr.speed_hz = config_.frequency_hz;
            tr.bits_per_word = 8;
            return (::ioctl(worker_.fd(), SPI_IOC_MESSAGE(1), &tr) >= 0);
        } else {
            size_t copy_n = std::min(tx_data.size(), rx_data.size());
            if (copy_n > 0) {
                std::memcpy(rx_data.data(), tx_data.data(), copy_n);
            }
            return true;
        }
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
ISpi& get_spi_driver() {
    return g_linux_spi;
}

} // namespace abstractx::hal
