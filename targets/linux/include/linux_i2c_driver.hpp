/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Linux HAL: i2c-dev driver (II2c) over I2cWorker.
 */

#ifndef ABSTRACTX_TARGET_LINUX_I2C_DRIVER_HPP
#define ABSTRACTX_TARGET_LINUX_I2C_DRIVER_HPP

#include "abstractx/hal/i2c.hpp"
#include "abstractx/hal/platform.hpp"
#include "abstractx/trace/tracer.hpp"
#include "i2c_worker.hpp"

namespace abstractx::hal {

// @impl [SPEC-LINUX-HAL-01] targets/linux/SPECIFICATION.md
class LinuxI2cDriver : public II2c {
public:
    explicit LinuxI2cDriver(const char* i2c_dev_path = "/dev/i2c-1") noexcept
        : i2c_dev_path_(i2c_dev_path) {
        worker_.set_completion_sink(
            target::I2cWorker::CompletionSink::create<LinuxI2cDriver, &LinuxI2cDriver::on_worker_done>(*this));
    }
    ~LinuxI2cDriver() override {
        worker_.stop();
    }

    bool init(const I2cConfig& config) override {
        config_ = config;
        return worker_.start(i2c_dev_path_, config.frequency_hz);
    }

    void shutdown() noexcept { worker_.stop(); }

    bool set_frequency(uint32_t frequency_hz) override {
        config_.frequency_hz = frequency_hz;
        return worker_.set_frequency(frequency_hz);
    }

    bool write_read_sync(uint8_t slave_addr,
                         std::span<const uint8_t> tx_data,
                         std::span<uint8_t> rx_data) override {
        uint64_t t0 = get_timer_driver().get_time_us();
        bool ok = false;
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
            ok = (::ioctl(worker_.fd(), I2C_RDWR, &rdwr) >= 0);
        } else {
            for (size_t i = 0; i < rx_data.size(); ++i) {
                rx_data[i] = static_cast<uint8_t>(0x30 + i);
            }
            ok = true;
        }
        uint64_t t1 = get_timer_driver().get_time_us();
        trace::g_tracer.trace_hal(3 /* I2C */, static_cast<uint16_t>(tx_data.size() + rx_data.size()),
                                  ok ? static_cast<uint16_t>(tx_data.size() + rx_data.size()) : 0,
                                  static_cast<uint32_t>(t1 - t0), ok ? 0 : 1, t1);
        return ok;
    }

    bool write_sync(uint8_t slave_addr, std::span<const uint8_t> tx_data) override {
        uint64_t t0 = get_timer_driver().get_time_us();
        bool ok = false;
        if (worker_.is_hardware_active()) {
            struct i2c_msg msg{};
            msg.addr = slave_addr;
            msg.flags = 0;
            msg.len = static_cast<uint16_t>(tx_data.size());
            msg.buf = const_cast<uint8_t*>(tx_data.data());

            struct i2c_rdwr_ioctl_data rdwr{};
            rdwr.msgs = &msg;
            rdwr.nmsgs = 1;
            ok = (::ioctl(worker_.fd(), I2C_RDWR, &rdwr) >= 0);
        } else {
            ok = true;
        }
        uint64_t t1 = get_timer_driver().get_time_us();
        trace::g_tracer.trace_hal(3 /* I2C */, static_cast<uint16_t>(tx_data.size()),
                                  ok ? static_cast<uint16_t>(tx_data.size()) : 0,
                                  static_cast<uint32_t>(t1 - t0), ok ? 0 : 1, t1);
        return ok;
    }

    bool read_sync(uint8_t slave_addr, std::span<uint8_t> rx_data) override {
        uint64_t t0 = get_timer_driver().get_time_us();
        bool ok = false;
        if (worker_.is_hardware_active()) {
            struct i2c_msg msg{};
            msg.addr = slave_addr;
            msg.flags = I2C_M_RD;
            msg.len = static_cast<uint16_t>(rx_data.size());
            msg.buf = rx_data.data();

            struct i2c_rdwr_ioctl_data rdwr{};
            rdwr.msgs = &msg;
            rdwr.nmsgs = 1;
            ok = (::ioctl(worker_.fd(), I2C_RDWR, &rdwr) >= 0);
        } else {
            for (size_t i = 0; i < rx_data.size(); ++i) {
                rx_data[i] = static_cast<uint8_t>(0x40 + i);
            }
            ok = true;
        }
        uint64_t t1 = get_timer_driver().get_time_us();
        trace::g_tracer.trace_hal(3 /* I2C */, static_cast<uint16_t>(rx_data.size()),
                                  ok ? static_cast<uint16_t>(rx_data.size()) : 0,
                                  static_cast<uint32_t>(t1 - t0), ok ? 0 : 1, t1);
        return ok;
    }

    target::I2cWorker& worker() noexcept {
        return worker_;
    }

protected:
    // @impl [SPEC-LINUX-HAL-01] targets/linux/SPECIFICATION.md
    void start_hardware_transfer_from_isr(const I2cRequest& req) noexcept override {
        if (!worker_.submit(req)) {
            I2cResult refused{};
            refused.status = I2cStatus::QueueFull;
            push_completion_from_isr(req, refused);
        }
        set_hardware_idle_from_isr();
    }

private:
    // Worker thread: deliver the result to the awaiter's result_slot, then post its handle.
    void on_worker_done(const I2cRequest& req, const I2cResult& res) noexcept {
        push_completion_from_isr(req, res);
    }

private:
    const char*        i2c_dev_path_;
    I2cConfig          config_{};
    target::I2cWorker  worker_{};
};

} // namespace abstractx::hal

#endif // ABSTRACTX_TARGET_LINUX_I2C_DRIVER_HPP
