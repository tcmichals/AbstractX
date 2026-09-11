/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Linux HAL: i2c-dev Driver with Dedicated Worker Thread
 */

#include "abstractx/hal/i2c.hpp"
#include "i2c_worker.hpp"
#include <cstring>
#include <algorithm>

namespace abstractx::hal {

class LinuxI2cDriver : public II2c {
public:
    LinuxI2cDriver() = default;
    ~LinuxI2cDriver() override {
        worker_.stop();
    }

    bool init(const I2cConfig& config) override {
        config_ = config;
        return worker_.start("/dev/i2c-1", config.frequency_hz);
    }

    bool set_frequency(uint32_t frequency_hz) override {
        config_.frequency_hz = frequency_hz;
        return worker_.set_frequency(frequency_hz);
    }

    bool write_read_sync(uint8_t slave_addr,
                         std::span<const uint8_t> tx_data,
                         std::span<uint8_t> rx_data) override {
        if (worker_.is_hardware_active()) {
            struct i2c_msg msgs[2]{};
            msgs[0].addr = slave_addr;
            msgs[0].flags = 0;
            msgs[0].len = static_cast<uint16_t>(tx_data.size());
            msgs[0].buf = const_cast<uint8_t*>(tx_data.data());

            msgs[1].addr = slave_addr;
            msgs[1].flags = I2C_M_RD;
            msgs[1].len = static_cast<uint16_t>(rx_data.size());
            msgs[1].buf = rx_data.data();

            struct i2c_rdwr_ioctl_data rdwr{};
            rdwr.msgs = msgs;
            rdwr.nmsgs = 2;
            return (::ioctl(worker_.fd(), I2C_RDWR, &rdwr) >= 0);
        } else {
            for (size_t i = 0; i < rx_data.size(); ++i) {
                rx_data[i] = static_cast<uint8_t>(0x30 + i);
            }
            return true;
        }
    }

    bool write_sync(uint8_t slave_addr, std::span<const uint8_t> tx_data) override {
        if (worker_.is_hardware_active()) {
            struct i2c_msg msg{};
            msg.addr = slave_addr;
            msg.flags = 0;
            msg.len = static_cast<uint16_t>(tx_data.size());
            msg.buf = const_cast<uint8_t*>(tx_data.data());

            struct i2c_rdwr_ioctl_data rdwr{};
            rdwr.msgs = &msg;
            rdwr.nmsgs = 1;
            return (::ioctl(worker_.fd(), I2C_RDWR, &rdwr) >= 0);
        }
        return true;
    }

    bool read_sync(uint8_t slave_addr, std::span<uint8_t> rx_data) override {
        if (worker_.is_hardware_active()) {
            struct i2c_msg msg{};
            msg.addr = slave_addr;
            msg.flags = I2C_M_RD;
            msg.len = static_cast<uint16_t>(rx_data.size());
            msg.buf = rx_data.data();

            struct i2c_rdwr_ioctl_data rdwr{};
            rdwr.msgs = &msg;
            rdwr.nmsgs = 1;
            return (::ioctl(worker_.fd(), I2C_RDWR, &rdwr) >= 0);
        } else {
            for (size_t i = 0; i < rx_data.size(); ++i) {
                rx_data[i] = static_cast<uint8_t>(0x40 + i);
            }
            return true;
        }
    }

    target::I2cWorker& worker() noexcept {
        return worker_;
    }

protected:
    void start_hardware_transfer_from_isr(const I2cRequest& req) noexcept override {
        worker_.submit(req);
        set_hardware_idle_from_isr();
    }

private:
    I2cConfig          config_{};
    target::I2cWorker  worker_{};
};

static LinuxI2cDriver g_linux_i2c;
II2c& get_i2c_driver() {
    return g_linux_i2c;
}

} // namespace abstractx::hal
