/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Host POSIX HAL SPI Implementation (Mock Loopback)
 */

#include "abstractx/hal/spi.hpp"
#include <cstring>
#include <algorithm>

namespace abstractx::hal {

class HostSpiLoopback : public ISpi {
public:
    bool init(const SpiConfig& config) override {
        (void)config;
        return true;
    }

    void select(bool active) override {
        (void)active;
    }

    uint8_t transfer_byte(uint8_t tx) override {
        return tx; // Loopback
    }

    bool transfer_sync(std::span<const uint8_t> tx_data, std::span<uint8_t> rx_data) override {
        size_t len = std::min(tx_data.size(), rx_data.size());
        if (len > 0) {
            std::memcpy(rx_data.data(), tx_data.data(), len);
        }
        return true;
    }

protected:
    void start_hardware_transfer_from_isr(const SpiRequest& req) noexcept override {
        SpiResult result{};
        result.status = SpiStatus::Ok;
        size_t len = std::min(req.tx_data.size(), req.rx_data.size());
        if (len > 0) {
            std::memcpy(req.rx_data.data(), req.tx_data.data(), len);
            result.transferred_bytes = len;
        }
        if (req.is_dual_spi) {
            size_t len_b = std::min(req.tx_data_link_b.size(), req.rx_data_link_b.size());
            if (len_b > 0) {
                std::memcpy(req.rx_data_link_b.data(), req.tx_data_link_b.data(), len_b);
                result.transferred_bytes += len_b;
            }
        }
        push_completion_from_isr(req, result);
        set_hardware_idle_from_isr();
    }
};

} // namespace abstractx::hal
