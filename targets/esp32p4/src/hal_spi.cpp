/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX ESP32-P4 HAL SPI Implementation
 */

#include "abstractx/hal/spi.hpp"

namespace abstractx::hal {

// @impl [SPEC-HAL-02] targets/SPECIFICATION.md#42-spi-interface-ispi--asyncspidriver
// @impl [SPEC-ESP32P4-HAL-00] targets/esp32p4/SPECIFICATION.md (no GDMA driver yet: refuse)
class Esp32p4Spi : public ISpi {
public:
    bool init(const SpiConfig& config) override {
        (void)config;
        return false;
    }

    void select(bool active) override {
        (void)active;
    }


protected:
    void start_hardware_transfer_from_isr(const SpiRequest& req) noexcept override {
        SpiResult result{};
        result.status = SpiStatus::NotSupported;
        result.transferred_bytes = 0;
        push_completion_from_isr(req, result);
        set_hardware_idle_from_isr();
    }
};

} // namespace abstractx::hal
