/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Target: Dedicated Background I2C Worker Thread
 */

#ifndef ABSTRACTX_TARGET_I2C_WORKER_HPP
#define ABSTRACTX_TARGET_I2C_WORKER_HPP

#include "bus_sim.hpp"
#include "eventfd.hpp"
#include "abstractx/hal/i2c.hpp"
#include <etl/queue_spsc_isr.h>
#include <etl/memory_model.h>
#include <etl/delegate.h>

#include <thread>
#include <atomic>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <cerrno>

namespace abstractx::target {

class I2cWorker {
public:
    using RequestQueue = etl::queue_spsc_isr<hal::I2cRequest, 16, InterruptLock, etl::memory_model::MEMORY_MODEL_SMALL>;

    // Driver hook that completes a request (AsyncDriverBase::push_completion_from_isr).
    using CompletionSink = etl::delegate<void(const hal::I2cRequest&, const hal::I2cResult&)>;

    I2cWorker() noexcept : wakeup_eventfd_(false) {}

    // Bind before start(); the worker thread reads it without a lock.
    void set_completion_sink(CompletionSink sink) noexcept { sink_ = sink; }

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
        bool was_running = false;
        {
            InterruptGuard lock;
            was_running = running_.exchange(false, std::memory_order_acq_rel);
        }
        if (was_running) {
            wakeup_eventfd_.notify(1);
            if (worker_thread_.joinable()) {
                worker_thread_.join();
            }
            if (fd_ >= 0) {
                ::close(fd_);
                fd_ = -1;
            }
            // Requests still queued would strand their awaiters: finish them as Cancelled.
            hal::I2cRequest req;
            while (queue_.pop(req)) {
                hal::I2cResult res{};
                res.status = hal::I2cStatus::Cancelled;
                complete(req, res);
            }
        }
    }

    // @impl [SPEC-LINUX-HAL-01] targets/linux/SPECIFICATION.md
    // false = refused (worker not running or queue full); the caller completes the request.
    bool submit(const hal::I2cRequest& req) noexcept {
        bool pushed = false;
        {
            InterruptGuard lock;
            if (running_.load(std::memory_order_acquire)) {
                pushed = queue_.push(req);
            }
        }
        if (!pushed) {
            return false;
        }
        (void)wakeup_eventfd_.notify(1);  // queued: never report it as refused
        return true;
    }

    bool set_frequency(uint32_t freq_hz) noexcept {
        speed_hz_ = freq_hz;
        return true;
    }

    bool is_running() const noexcept { return running_.load(std::memory_order_acquire); }
    int fd() const noexcept { return fd_; }
    bool is_hardware_active() const noexcept { return fd_ >= 0; }

private:
    static hal::I2cStatus status_from_errno(int e) noexcept {
        switch (e) {
            case ENXIO: case EREMOTEIO: return hal::I2cStatus::NackAddress;
            case ETIMEDOUT:             return hal::I2cStatus::Timeout;
            case EAGAIN:                return hal::I2cStatus::ArbitrationLost;
            case ENOTTY: case EOPNOTSUPP: return hal::I2cStatus::NotSupported;
            case EINVAL:                return hal::I2cStatus::InvalidArgument;
            default:                    return hal::I2cStatus::BusError;
        }
    }

    // @impl [SPEC-HAL-09] docs/DESIGN_SPECIFICATION.md (message list -> one I2C_RDWR)
    hal::I2cResult transfer_msgs(const hal::I2cRequest& req) noexcept {
        hal::I2cResult res{};
        constexpr size_t kMaxMsgs = 42;  // I2C_RDWR_IOCTL_MAX_MSGS
        if (req.msgs.size() > kMaxMsgs) {
            res.status = hal::I2cStatus::NotSupported;
            return res;
        }
        const bool ten_bit = (req.address & hal::kI2cTenBit) != 0;
        const uint16_t addr = static_cast<uint16_t>(req.address & (ten_bit ? 0x3FFU : 0x7FU));
        struct i2c_msg msgs[kMaxMsgs]{};
        size_t bytes = 0;
        for (size_t i = 0; i < req.msgs.size(); ++i) {
            const hal::I2cMsg& m = req.msgs[i];
            uint16_t f = 0;
            if (m.is_read())                        f |= I2C_M_RD;
            if (m.flags & hal::I2cFlag::NoStart)    f |= I2C_M_NOSTART;
            if (m.flags & hal::I2cFlag::Stop)       f |= I2C_M_STOP;
            if (m.flags & hal::I2cFlag::IgnoreNack) f |= I2C_M_IGNORE_NAK;
            if (m.flags & hal::I2cFlag::RecvLen)    f |= I2C_M_RECV_LEN;
            if (ten_bit)                            f |= I2C_M_TEN;
            msgs[i].addr = addr;
            msgs[i].flags = f;
            msgs[i].len = static_cast<uint16_t>(m.size());
            msgs[i].buf = m.is_read() ? m.rx.data() : const_cast<uint8_t*>(m.tx.data());
            bytes += m.size();
        }
        struct i2c_rdwr_ioctl_data rdwr{};
        rdwr.msgs = msgs;
        rdwr.nmsgs = static_cast<uint32_t>(req.msgs.size());
        const int ret = ::ioctl(fd_, I2C_RDWR, &rdwr);
        if (ret < 0) {
            res.status = status_from_errno(errno);
            return res;
        }
        res.msgs_done = static_cast<uint8_t>(ret);
        res.transferred_bytes = bytes;
        res.status = (static_cast<size_t>(ret) == req.msgs.size()) ? hal::I2cStatus::Ok : hal::I2cStatus::NackData;
        return res;
    }

    void worker_loop() noexcept {
        while (running_.load(std::memory_order_acquire)) {
            wakeup_eventfd_.wait_blocking();
            if (!running_.load(std::memory_order_acquire)) break;

            hal::I2cRequest req;
            while (queue_.pop(req)) {
                if (req.abandoned()) {  // [SPEC-ARCH-16] 8: its awaiter is gone; do not touch its buffers
                    complete(req, hal::I2cResult{});
                    continue;
                }
                hal::I2cResult res{};
                res.status = hal::I2cStatus::Ok;

                // @impl [SPEC-HAL-09] docs/DESIGN_SPECIFICATION.md (never Ok without a transfer)
                // A message list goes to the kernel as-is: one I2C_RDWR, every shape.
                if (!req.msgs.empty() && fd_ >= 0) {
                    res = transfer_msgs(req);
                    res.timestamp_us = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count());
                    complete(req, res);
                    continue;
                }

                // Normalize [SPEC-HAL-09] req.msgs into req.slave_addr / tx_data / rx_data when present
                if (!req.msgs.empty()) {
                    req.slave_addr = static_cast<uint8_t>(req.address & 0x7FU);
                    if (req.msgs.size() == 1) {
                        if (req.msgs[0].is_read()) {
                            req.tx_data = {};
                            req.rx_data = req.msgs[0].rx;
                        } else {
                            req.tx_data = req.msgs[0].tx;
                            req.rx_data = {};
                        }
                        req.repeated_start = false;
                    } else if (req.msgs.size() == 2 && !req.msgs[0].is_read() && req.msgs[1].is_read()) {
                        req.tx_data = req.msgs[0].tx;
                        req.rx_data = req.msgs[1].rx;
                        req.repeated_start = true;
                        if (req.tx_data.size() == 1) {
                            req.use_register = true;
                            req.register_offset = req.tx_data[0];
                        }
                    }
                    res.msgs_done = static_cast<uint8_t>(req.msgs.size());
                }

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
                    } else if (req.rx_data.empty()) {
                        // Zero-length write: the address probe ([SPEC-HAL-09])
                        struct i2c_msg msg{};
                        msg.addr = req.slave_addr;
                        msg.flags = 0;
                        msg.len = 0;
                        msg.buf = nullptr;
                        struct i2c_rdwr_ioctl_data rdwr{};
                        rdwr.msgs = &msg;
                        rdwr.nmsgs = 1;
                        if (::ioctl(fd_, I2C_RDWR, &rdwr) < 0) {
                            res.status = status_from_errno(errno);
                        }
                        res.transferred_bytes = 0;
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
                } else if (!::abstractx::hal::linux_bus_sim_allowed()) {
                    // No device and no simulation asked for: fail ([SPEC-LINUX-HAL-01] no silent simulation)
                    res.status = hal::I2cStatus::BusError;
                    res.transferred_bytes = 0;
                } else {
                    // Simulation, only with ABSTRACTX_SIM_BUS=1 (host tests, SITL)
                    if (!req.rx_data.empty()) {
                        if (req.use_register && req.register_offset == 0xD0) {
                            req.rx_data[0] = 0x58; // BMP280 chip ID
                        } else if ((req.slave_addr == 0x0D || req.slave_addr == 0x1E) && req.rx_data.size() >= 6) {
                            // Synthesize realistic 3-axis magnetic field vector (X=250mG, Y=100mG, Z=450mG)
                            int16_t mx = 750;
                            int16_t my = 300;
                            int16_t mz = 1350;
                            req.rx_data[0] = static_cast<uint8_t>(mx & 0xFF);
                            req.rx_data[1] = static_cast<uint8_t>((mx >> 8) & 0xFF);
                            req.rx_data[2] = static_cast<uint8_t>(my & 0xFF);
                            req.rx_data[3] = static_cast<uint8_t>((my >> 8) & 0xFF);
                            req.rx_data[4] = static_cast<uint8_t>(mz & 0xFF);
                            req.rx_data[5] = static_cast<uint8_t>((mz >> 8) & 0xFF);
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
                res.timestamp_us = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(end_time.time_since_epoch()).count());

                complete(req, res);
            }
        }
    }

    // @impl [SPEC-LINUX-HAL-01] targets/linux/SPECIFICATION.md
    // The awaiter's result_slot must be written before its handle is posted.
    void complete(const hal::I2cRequest& req, const hal::I2cResult& res) noexcept {
        if (sink_.is_valid()) {
            sink_(req, res);
            return;
        }
        req.complete(res);  // [SPEC-ARCH-16] 8: result, post and Done in one place (skips if abandoned)
    }

    CompletionSink       sink_{};
    int                  fd_{-1};
    uint32_t             speed_hz_{400'000};
    RequestQueue         queue_{};
    EventFd              wakeup_eventfd_;
    std::atomic<bool>    running_{false};
    std::thread          worker_thread_{};
};

} // namespace abstractx::target

#endif // ABSTRACTX_TARGET_I2C_WORKER_HPP
