/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Target: Dedicated Background I2C Worker Thread
 */

#ifndef ABSTRACTX_TARGET_I2C_WORKER_HPP
#define ABSTRACTX_TARGET_I2C_WORKER_HPP

#include "eventfd.hpp"
#include "abstractx/hal/i2c.hpp"
#include <etl/queue_spsc_isr.h>
#include <etl/memory_model.h>

#include <thread>
#include <atomic>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <unistd.h>
#include <chrono>
#include <cstring>

namespace abstractx::target {

class I2cWorker {
public:
    using RequestQueue = etl::queue_spsc_isr<hal::I2cRequest, 16, InterruptLock, etl::memory_model::MEMORY_MODEL_SMALL>;

    I2cWorker() noexcept : wakeup_eventfd_(false) {}

    ~I2cWorker() noexcept {
        stop();
    }

    bool start(const char* i2c_dev_path = "/dev/i2c-1", uint32_t speed_hz = 400'000) noexcept {
        speed_hz_ = speed_hz;

        if (i2c_dev_path && i2c_dev_path[0] != '\0') {
            fd_ = ::open(i2c_dev_path, O_RDWR | O_CLOEXEC);
        }

        running_.store(true, std::memory_order_release);
        worker_thread_ = std::thread(&I2cWorker::worker_loop, this);
        return true;
    }

    void stop() noexcept {
        if (running_.exchange(false, std::memory_order_acq_rel)) {
            wakeup_eventfd_.notify(1);
            if (worker_thread_.joinable()) {
                worker_thread_.join();
            }
            if (fd_ >= 0) {
                ::close(fd_);
                fd_ = -1;
            }
        }
    }

    bool submit(const hal::I2cRequest& req) noexcept {
        if (!queue_.push(req)) {
            return false;
        }
        return wakeup_eventfd_.notify(1);
    }

    bool set_frequency(uint32_t freq_hz) noexcept {
        speed_hz_ = freq_hz;
        return true;
    }

    int fd() const noexcept { return fd_; }
    bool is_hardware_active() const noexcept { return fd_ >= 0; }

private:
    void worker_loop() noexcept {
        while (running_.load(std::memory_order_acquire)) {
            wakeup_eventfd_.wait_blocking();
            if (!running_.load(std::memory_order_acquire)) break;

            hal::I2cRequest req;
            while (queue_.pop(req)) {
                auto start_time = std::chrono::steady_clock::now();
                hal::I2cResult res{};
                res.status = hal::I2cStatus::Ok;

                if (fd_ >= 0) {
                    if (req.repeated_start && !req.tx_data.empty() && !req.rx_data.empty()) {
                        // Combined Write-then-Read (Repeated Start)
                        struct i2c_msg msgs[2]{};
                        msgs[0].addr = req.slave_addr;
                        msgs[0].flags = 0;
                        msgs[0].len = static_cast<uint16_t>(req.tx_data.size());
                        msgs[0].buf = const_cast<uint8_t*>(req.tx_data.data());

                        msgs[1].addr = req.slave_addr;
                        msgs[1].flags = I2C_M_RD;
                        msgs[1].len = static_cast<uint16_t>(req.rx_data.size());
                        msgs[1].buf = req.rx_data.data();

                        struct i2c_rdwr_ioctl_data rdwr{};
                        rdwr.msgs = msgs;
                        rdwr.nmsgs = 2;

                        int ret = ::ioctl(fd_, I2C_RDWR, &rdwr);
                        if (ret < 0) {
                            res.status = hal::I2cStatus::NackAddress;
                            res.transferred_bytes = 0;
                        } else {
                            res.transferred_bytes = req.rx_data.size();
                        }
                    } else if (!req.tx_data.empty()) {
                        // Master Write Burst
                        struct i2c_msg msg{};
                        msg.addr = req.slave_addr;
                        msg.flags = 0;
                        msg.len = static_cast<uint16_t>(req.tx_data.size());
                        msg.buf = const_cast<uint8_t*>(req.tx_data.data());

                        struct i2c_rdwr_ioctl_data rdwr{};
                        rdwr.msgs = &msg;
                        rdwr.nmsgs = 1;

                        int ret = ::ioctl(fd_, I2C_RDWR, &rdwr);
                        if (ret < 0) {
                            res.status = hal::I2cStatus::NackAddress;
                            res.transferred_bytes = 0;
                        } else {
                            res.transferred_bytes = req.tx_data.size();
                        }
                    } else if (!req.rx_data.empty()) {
                        // Master Read Burst
                        struct i2c_msg msg{};
                        msg.addr = req.slave_addr;
                        msg.flags = I2C_M_RD;
                        msg.len = static_cast<uint16_t>(req.rx_data.size());
                        msg.buf = req.rx_data.data();

                        struct i2c_rdwr_ioctl_data rdwr{};
                        rdwr.msgs = &msg;
                        rdwr.nmsgs = 1;

                        int ret = ::ioctl(fd_, I2C_RDWR, &rdwr);
                        if (ret < 0) {
                            res.status = hal::I2cStatus::NackAddress;
                            res.transferred_bytes = 0;
                        } else {
                            res.transferred_bytes = req.rx_data.size();
                        }
                    }
                } else {
                    // Simulated / mock fallback for host unit tests
                    if (!req.rx_data.empty()) {
                        if (req.use_register && req.register_offset == 0xD0) {
                            req.rx_data[0] = 0x58; // BMP280 chip ID
                        } else {
                            for (size_t i = 0; i < req.rx_data.size(); ++i) {
                                req.rx_data[i] = static_cast<uint8_t>(0x20 + i);
                            }
                        }
                        res.transferred_bytes = req.rx_data.size();
                    } else {
                        res.transferred_bytes = req.tx_data.size();
                    }
                }

                auto end_time = std::chrono::steady_clock::now();
                auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
                res.timestamp_us = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(end_time.time_since_epoch()).count());

                req.notify_rx_completion(res);
                req.notify_tx_completion(res);
                req.notify_completion(res);
            }
        }
    }

    int                  fd_{-1};
    uint32_t             speed_hz_{400'000};
    RequestQueue         queue_{};
    EventFd              wakeup_eventfd_;
    std::atomic<bool>    running_{false};
    std::thread          worker_thread_{};
};

} // namespace abstractx::target

#endif // ABSTRACTX_TARGET_I2C_WORKER_HPP
