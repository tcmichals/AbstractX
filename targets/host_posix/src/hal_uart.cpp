/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Host POSIX HAL UART Implementation
 */

#include "abstractx/hal/uart.hpp"
#include <iostream>

namespace abstractx::hal {

class HostUart : public IUart {
public:
    void init(uint32_t baudrate) override {
        (void)baudrate;
    }

    void write_byte(uint8_t ch) override {
        std::cout.put(static_cast<char>(ch));
    }

    void puts(const char* str) override {
        std::cout << str << std::flush;
    }

    size_t write(std::span<const uint8_t> buffer) override {
        std::cout.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        return buffer.size();
    }

    bool read_byte(uint8_t& ch) override {
        if (std::cin.rdbuf()->in_avail() > 0) {
            int c = std::cin.get();
            if (c != EOF) {
                ch = static_cast<uint8_t>(c);
                return true;
            }
        }
        return false;
    }

    size_t read(std::span<uint8_t> buffer) override {
        size_t count = 0;
        for (uint8_t& b : buffer) {
            if (read_byte(b)) {
                count++;
            } else {
                break;
            }
        }
        return count;
    }

    void flush() override {
        std::cout.flush();
    }

protected:
    void start_hardware_transfer_from_isr(const UartTxRequest& req) noexcept override {
        UartResult result{};
        result.status = UartStatus::Ok;
        if (!req.data.empty()) {
            std::cout.write(reinterpret_cast<const char*>(req.data.data()), req.data.size());
            result.bytes_transferred = req.data.size();
        }
        push_completion_from_isr(req, result);
        set_hardware_idle_from_isr();
    }
};

} // namespace abstractx::hal
