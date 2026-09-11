/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX ESP32-P4 HAL SPI Implementation
 */

#include "abstractx/hal/spi.hpp"

namespace abstractx::hal {

class Esp32p4Spi : public ISpi {
public:
    bool init(const SpiConfig& config) override {
        (void)config;
        return true;
    }

    void select(bool active) override {
        (void)active;
    }

    uint8_t transfer_byte(uint8_t tx) override {
        return tx;
    }

    bool transfer_sync(std::span<const uint8_t> tx_data, std::span<uint8_t> rx_data) override {
        (void)tx_data;
        (void)rx_data;
        return true;
    }

protected:
    void start_hardware_transfer_from_isr(const SpiRequest& req) noexcept override {
        // ESP32-P4 GDMA SPI driver trigger
        SpiResult result{};
        result.status = SpiStatus::Ok;
        result.transferred_bytes = req.tx_data.size();
        push_completion_from_isr(req, result);
        set_hardware_idle_from_isr();
    }
};

} // namespace abstractx::hal
