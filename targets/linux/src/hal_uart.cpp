/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Linux HAL: termios2 Non-Blocking Serial Driver with Low Latency
 */

#include "abstractx/hal/uart.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <asm/termbits.h>
#include <linux/serial.h>
#include <iostream>
#include <cstring>

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
            if (std::cin.rdbuf()->in_avail() > 0) {
                int c = std::cin.get();
                if (c != EOF) {
                    buffer[0] = static_cast<uint8_t>(c);
                    return 1;
                }
            }
            return 0;
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
        UartResult result{};
        result.status = UartStatus::Ok;
        if (!req.data.empty()) {
            result.bytes_transferred = write(req.data);
        }
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

    int      fd_{-1};
    uint32_t baudrate_{115200};
};

static LinuxUartDriver g_linux_uart;
IUart& get_uart_driver() {
    return g_linux_uart;
}

} // namespace abstractx::hal
