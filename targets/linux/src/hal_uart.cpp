/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Linux HAL: termios2 Non-Blocking Serial Driver with Low Latency
 */

#include "abstractx/hal/uart.hpp"
#include "abstractx/hal/platform.hpp"
#include "abstractx/trace/tracer.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <asm/termbits.h>
#include <linux/serial.h>
#include <iostream>
#include <cstring>
#include <deque>
#include <chrono>

namespace abstractx::hal {

class LinuxUartDriver : public IUart {
public:
    LinuxUartDriver() = default;

    ~LinuxUartDriver() override {
        close();
    }

    bool open(const char* device_path, uint32_t baudrate = 115200) noexcept {
        close();
        baudrate_ = baudrate;

        if (device_path && device_path[0] != '\0') {
            fd_ = ::open(device_path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
            if (fd_ >= 0) {
                configure_termios2(baudrate);
                configure_low_latency();
                return true;
            }
        }
        return false;
    }

    void close() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    void init(uint32_t baudrate) override {
        baudrate_ = baudrate;
        if (fd_ >= 0) {
            configure_termios2(baudrate);
        }
    }

    bool set_baud_rate(uint32_t baudrate) override {
        baudrate_ = baudrate;
        if (fd_ >= 0) {
            return configure_termios2(baudrate);
        }
        return true;
    }

    void write_byte(uint8_t ch) override {
        write(std::span<const uint8_t>(&ch, 1));
    }

    void puts(const char* str) override {
        if (!str) return;
        size_t len = std::strlen(str);
        write(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(str), len));
    }

    size_t write(std::span<const uint8_t> buffer) override {
        if (buffer.empty()) return 0;

        if (fd_ >= 0) {
            ssize_t ret = ::write(fd_, buffer.data(), buffer.size());
            return (ret > 0) ? static_cast<size_t>(ret) : 0;
        } else {
            std::cout.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
            std::cout.flush();
            return buffer.size();
        }
    }

    bool read_byte(uint8_t& ch) override {
        return (read(std::span<uint8_t>(&ch, 1)) == 1);
    }

    size_t read(std::span<uint8_t> buffer) override {
        if (buffer.empty()) return 0;

        if (fd_ >= 0) {
            ssize_t ret = ::read(fd_, buffer.data(), buffer.size());
            return (ret > 0) ? static_cast<size_t>(ret) : 0;
        } else {
            // SITL Simulated U-Blox UBX-NAV-PVT GPS generator (10 Hz)
            ensure_sitl_gps_stream();
            size_t count = 0;
            while (count < buffer.size() && !sitl_rx_queue_.empty()) {
                buffer[count++] = sitl_rx_queue_.front();
                sitl_rx_queue_.pop_front();
            }
            return count;
        }
    }

    void flush() override {
        if (fd_ >= 0) {
            ::ioctl(fd_, TCSBRK, 1);
        } else {
            std::cout.flush();
        }
    }

    int fd() const noexcept { return fd_; }
    bool is_hardware_active() const noexcept { return fd_ >= 0; }

protected:
    void start_hardware_transfer_from_isr(const UartTxRequest& req) noexcept override {
        uint64_t t0 = get_timer_driver().get_time_us();
        UartResult result{};
        result.status = UartStatus::Ok;
        if (!req.data.empty()) {
            result.bytes_transferred = write(req.data);
        }
        uint64_t t1 = get_timer_driver().get_time_us();
        trace::g_tracer.trace_hal(1 /* UART */, static_cast<uint16_t>(req.data.size()),
                                  static_cast<uint16_t>(result.bytes_transferred),
                                  static_cast<uint32_t>(t1 - t0),
                                  static_cast<uint8_t>(result.status), t1);
        push_completion_from_isr(req, result);
        set_hardware_idle_from_isr();
    }

private:
    bool configure_termios2(uint32_t baud) noexcept {
        if (fd_ < 0) return false;

        struct termios2 tio{};
        if (::ioctl(fd_, TCGETS2, &tio) < 0) return false;

        tio.c_cflag &= ~CBAUD;
        tio.c_cflag |= BOTHER;
        tio.c_ispeed = baud;
        tio.c_ospeed = baud;

        // 8N1 raw mode
        tio.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS);
        tio.c_cflag |= (CS8 | CREAD | CLOCAL);

        tio.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF | IXANY);
        tio.c_oflag &= ~OPOST;
        tio.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

        tio.c_cc[VMIN]  = 0;
        tio.c_cc[VTIME] = 0;

        return (::ioctl(fd_, TCSETS2, &tio) == 0);
    }

    void configure_low_latency() noexcept {
        if (fd_ < 0) return;
        struct serial_struct ser_info{};
        if (::ioctl(fd_, TIOCGSERIAL, &ser_info) == 0) {
            ser_info.flags |= ASYNC_LOW_LATENCY;
            ::ioctl(fd_, TIOCSSERIAL, &ser_info);
        }
    }

    void ensure_sitl_gps_stream() noexcept {
        auto now = std::chrono::steady_clock::now();
        auto diff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_sitl_gps_time_).count();
        if (diff_ms >= 100) { // 10 Hz
            last_sitl_gps_time_ = now;
            static uint32_t s_itow = 3600000;
            static int32_t s_lat = 377749000;
            static int32_t s_lon = -1224194000;
            static int32_t s_alt = 120000;
            s_itow += 100;
            s_alt += 50; // climb slightly
            if (s_alt > 300000) s_alt = 120000;

            uint8_t pkt[100];
            pkt[0] = 0xB5; pkt[1] = 0x62; // Header
            pkt[2] = 0x01; pkt[3] = 0x07; // NAV-PVT
            pkt[4] = 92;   pkt[5] = 0;    // Len = 92
            std::memset(&pkt[6], 0, 92);
            std::memcpy(&pkt[6 + 0], &s_itow, 4);
            pkt[6 + 20] = 3;  // fixType: 3D Fix
            pkt[6 + 21] = 1;  // flags: gnssFixOK
            pkt[6 + 23] = 18; // numSV: 18 satellites
            std::memcpy(&pkt[6 + 24], &s_lon, 4);
            std::memcpy(&pkt[6 + 28], &s_lat, 4);
            std::memcpy(&pkt[6 + 36], &s_alt, 4);
            int32_t gspeed = 14500; // 14.5 m/s
            int32_t heading = 9000000; // 90 deg
            std::memcpy(&pkt[6 + 60], &gspeed, 4);
            std::memcpy(&pkt[6 + 64], &heading, 4);

            uint8_t ck_a = 0, ck_b = 0;
            for (size_t i = 2; i < 98; ++i) {
                ck_a += pkt[i];
                ck_b += ck_a;
            }
            pkt[98] = ck_a;
            pkt[99] = ck_b;

            for (size_t i = 0; i < 100; ++i) {
                sitl_rx_queue_.push_back(pkt[i]);
            }
        }
    }

    int      fd_{-1};
    uint32_t baudrate_{115200};
    std::deque<uint8_t> sitl_rx_queue_{};
    std::chrono::steady_clock::time_point last_sitl_gps_time_{};
};

static LinuxUartDriver g_linux_uart;
IUart& get_uart_driver() noexcept {
    return g_linux_uart;
}

} // namespace abstractx::hal
