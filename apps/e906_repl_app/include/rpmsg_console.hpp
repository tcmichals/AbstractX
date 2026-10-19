/*
 * rpmsg_console.hpp - VirtIO RPMsg Console for MicroPython REPL
 *
 * @impl [SPEC-E906-REPL-03] [SPEC-E906-REPL-05] apps/e906_repl_app/SPECIFICATION.md
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <coroutine>
#include <etl/queue_spsc_isr.h>
#include "abstractx/interrupt_lock.hpp"
#include "asp_coro.hpp"
#include "hal/rpmsg.hpp"

namespace abstractx {

class RpmsgConsole {
public:
    static constexpr uint32_t REPL_ENDPOINT_ADDR = 1026;
    static constexpr size_t INPUT_BUFFER_SIZE = 512;
    static constexpr size_t OUTPUT_BUFFER_SIZE = 256;

    explicit RpmsgConsole(::hal::IRpmsg &rpmsg) noexcept;

    // Register RPMsg endpoint and announce service
    // @impl [SPEC-E906-REPL-05] apps/e906_repl_app/SPECIFICATION.md
    void init() noexcept;

    // Feed characters into the input queue (called from RPMsg endpoint callback)
    // @impl [SPEC-E906-REPL-03] apps/e906_repl_app/SPECIFICATION.md
    void on_rx_data(const uint8_t *data, size_t len) noexcept;

    // Send text to the host console (batches up to OUTPUT_BUFFER_SIZE or flush)
    // @impl [SPEC-E906-REPL-05] apps/e906_repl_app/SPECIFICATION.md
    void write(const char *str, size_t len) noexcept;
    void flush() noexcept;

    // Check if input characters are available
    [[nodiscard]] bool has_char() const noexcept {
        return !m_input_queue.empty();
    }

    // Try to peek at the next character without removing it from the queue
    // @impl [SPEC-E906-REPL-04] apps/e906_repl_app/SPECIFICATION.md
    bool try_peek_char(char &out_ch) const noexcept {
        if (!m_input_queue.empty()) {
            out_ch = static_cast<char>(m_input_queue.front());
            return true;
        }
        return false;
    }

    // Try to pop a single character without suspending
    bool try_read_char(char &out_ch) noexcept {
        uint8_t byte = 0;
        if (m_input_queue.pop(byte)) {
            out_ch = static_cast<char>(byte);
            return true;
        }
        return false;
    }

    // C++20 Awaiter for reading the next character
    // @impl [SPEC-E906-REPL-03] apps/e906_repl_app/SPECIFICATION.md
    struct ReadCharAwaiter {
        RpmsgConsole &console;

        bool await_ready() const noexcept {
            return console.has_char();
        }

        void await_suspend(std::coroutine_handle<> handle) noexcept {
            console.m_rpmsg.async_receive().await_suspend(handle);
        }

        char await_resume() noexcept {
            console.m_rpmsg.async_receive().await_resume();
            char ch = 0;
            console.try_read_char(ch);
            return ch;
        }
    };

    [[nodiscard]] ReadCharAwaiter read_char_async() noexcept {
        return ReadCharAwaiter{*this};
    }

private:
    static void rx_callback(const ::hal::RpmsgMessage &msg, void *user_data) noexcept;

    ::hal::IRpmsg &m_rpmsg;
    etl::queue_spsc_isr<uint8_t, INPUT_BUFFER_SIZE, InterruptLock> m_input_queue{};
    char m_out_buf[OUTPUT_BUFFER_SIZE]{};
    size_t m_out_len{0};
    uint32_t m_host_addr{0};
    bool m_host_known{false};
};

// Global singleton instance for MicroPython HAL bridging
extern RpmsgConsole *g_repl_console;

} // namespace abstractx
