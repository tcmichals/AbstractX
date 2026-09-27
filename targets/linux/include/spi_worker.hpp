/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Target: Dedicated Background SPI Worker Thread
 */

#ifndef ABSTRACTX_TARGET_SPI_WORKER_HPP
#define ABSTRACTX_TARGET_SPI_WORKER_HPP

#include "eventfd.hpp"
#include "abstractx/hal/spi.hpp"
#include <etl/queue_spsc_isr.h>
#include <etl/memory_model.h>

#include <thread>
#include <atomic>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <unistd.h>
#include <chrono>
#include <cstring>

namespace abstractx::target {

class SpiWorker {
public:
    using RequestQueue = etl::queue_spsc_isr<hal::SpiRequest, 16, InterruptLock, etl::memory_model::MEMORY_MODEL_SMALL>;

    SpiWorker() noexcept : wakeup_eventfd_(false) {}

    ~SpiWorker() noexcept {
        stop();
    }

    bool start(const char* spidev_path = "/dev/spidev1.0", uint32_t speed_hz = 10'000'000, uint8_t mode = 0) noexcept {
        speed_hz_ = speed_hz;
        mode_ = mode;

        if (spidev_path && spidev_path[0] != '\0') {
            fd_ = ::open(spidev_path, O_RDWR | O_CLOEXEC);
            if (fd_ >= 0) {
                ::ioctl(fd_, SPI_IOC_WR_MODE, &mode_);
                ::ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz_);
            }
        }

        running_.store(true, std::memory_order_release);
        worker_thread_ = std::thread(&SpiWorker::worker_loop, this);
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

    bool submit(const hal::SpiRequest& req) noexcept {
        if (!queue_.push(req)) {
            return false;
        }
        return wakeup_eventfd_.notify(1);
    }

    bool set_frequency(uint32_t freq_hz) noexcept {
        speed_hz_ = freq_hz;
        if (fd_ >= 0) {
            return (::ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz_) == 0);
        }
        return true;
    }

    int fd() const noexcept { return fd_; }
    bool is_hardware_active() const noexcept { return fd_ >= 0; }

private:
    void worker_loop() noexcept {
        while (running_.load(std::memory_order_acquire)) {
            wakeup_eventfd_.wait_blocking();
            if (!running_.load(std::memory_order_acquire)) break;

            hal::SpiRequest req;
            while (queue_.pop(req)) {
                auto start_time = std::chrono::steady_clock::now();
                hal::SpiResult res{};
                res.status = hal::SpiStatus::Ok;

                size_t xfer_len = req.tx_data.size();
                if (req.rx_data.size() > xfer_len) {
                    xfer_len = req.rx_data.size();
                }

                if (fd_ >= 0 && xfer_len > 0) {
                    struct spi_ioc_transfer tr{};
                    tr.tx_buf = reinterpret_cast<uint64_t>(req.tx_data.data());
                    tr.rx_buf = reinterpret_cast<uint64_t>(req.rx_data.data());
                    tr.len = static_cast<uint32_t>(xfer_len);
                    tr.speed_hz = speed_hz_;
                    tr.bits_per_word = 8;
                    tr.cs_change = req.auto_cs ? 0 : 1;

                    int ret = ::ioctl(fd_, SPI_IOC_MESSAGE(1), &tr);
                    if (ret < 0) {
                        res.status = hal::SpiStatus::DmaError;
                        res.transferred_bytes = 0;
                    } else {
                        res.transferred_bytes = xfer_len;
                    }
                } else {
                    // Simulated / mock fallback for host unit tests
                    if (!req.rx_data.empty()) {
                        if (!req.tx_data.empty() && (req.tx_data[0] & 0x7F) == 0x75) {
                            // ICM-42688 WHO_AM_I register simulation
                            req.rx_data[0] = 0x00;
                            if (req.rx_data.size() > 1) req.rx_data[1] = 0x47;
                        } else if (!req.tx_data.empty() && (req.tx_data[0] & 0x7F) == 0x1D && req.rx_data.size() >= 15) {
                            // SITL Synthetic Flight Dynamics (sinusoidal banking, pitching, gyro rates)
                            static uint64_t s_sim_step = 0;
                            s_sim_step++;
                            double t = static_cast<double>(s_sim_step) * 0.002;
                            double roll = 0.28 * std::sin(t * 1.8);
                            double pitch = 0.12 * std::cos(t * 1.2);
                            double gx = (0.28 * 1.8 * std::cos(t * 1.8)) * (180.0 / 3.14159265);
                            double gy = (-0.12 * 1.2 * std::sin(t * 1.2)) * (180.0 / 3.14159265);
                            double gz = 4.5; // gentle yaw turn
                            double ax = -std::sin(pitch);
                            double ay = std::sin(roll) * std::cos(pitch);
                            double az = std::cos(roll) * std::cos(pitch);

                            req.rx_data[0] = 0x00;
                            req.rx_data[1] = 0x00; req.rx_data[2] = 0x00; // Temp ~ 25 C
                            int16_t raw_ax = static_cast<int16_t>(ax * 2048.0);
                            int16_t raw_ay = static_cast<int16_t>(ay * 2048.0);
                            int16_t raw_az = static_cast<int16_t>(az * 2048.0);
                            req.rx_data[3] = static_cast<uint8_t>(raw_ax >> 8); req.rx_data[4] = static_cast<uint8_t>(raw_ax & 0xFF);
                            req.rx_data[5] = static_cast<uint8_t>(raw_ay >> 8); req.rx_data[6] = static_cast<uint8_t>(raw_ay & 0xFF);
                            req.rx_data[7] = static_cast<uint8_t>(raw_az >> 8); req.rx_data[8] = static_cast<uint8_t>(raw_az & 0xFF);
                            int16_t raw_gx = static_cast<int16_t>(gx * 16.4);
                            int16_t raw_gy = static_cast<int16_t>(gy * 16.4);
                            int16_t raw_gz = static_cast<int16_t>(gz * 16.4);
                            req.rx_data[9] = static_cast<uint8_t>(raw_gx >> 8); req.rx_data[10] = static_cast<uint8_t>(raw_gx & 0xFF);
                            req.rx_data[11] = static_cast<uint8_t>(raw_gy >> 8); req.rx_data[12] = static_cast<uint8_t>(raw_gy & 0xFF);
                            req.rx_data[13] = static_cast<uint8_t>(raw_gz >> 8); req.rx_data[14] = static_cast<uint8_t>(raw_gz & 0xFF);
                        } else {
                            size_t copy_n = std::min(req.tx_data.size(), req.rx_data.size());
                            if (copy_n > 0) {
                                std::memcpy(req.rx_data.data(), req.tx_data.data(), copy_n);
                            }
                        }
                    }
                    res.transferred_bytes = xfer_len;
                }

                auto end_time = std::chrono::steady_clock::now();
                auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
                res.bus_duration_us = static_cast<uint32_t>(duration_us);
                res.timestamp_us = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(end_time.time_since_epoch()).count());

                req.notify_rx_completion(res);
                req.notify_tx_completion(res);
                req.notify_completion(res);
            }
        }
    }

    int                  fd_{-1};
    uint32_t             speed_hz_{10'000'000};
    uint8_t              mode_{0};
    RequestQueue         queue_{};
    EventFd              wakeup_eventfd_;
    std::atomic<bool>    running_{false};
    std::thread          worker_thread_{};
};

} // namespace abstractx::target

#endif // ABSTRACTX_TARGET_SPI_WORKER_HPP
