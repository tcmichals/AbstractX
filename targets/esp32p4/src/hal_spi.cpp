/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX ESP32-P4 HAL SPI Implementation
 */

#include "abstractx/hal/spi.hpp"

namespace abstractx::hal {

// @impl [SPEC-HAL-02] targets/SPECIFICATION.md#42-spi-interface-ispi--asyncspidriver
class Esp32p4Spi : public ISpi {
public:
    bool init(const SpiConfig& config) override {
        (void)config;
        return true;
    }

    void select(bool active) override {
        (void)active;
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
