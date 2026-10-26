/*
 * rpmsg_console.hpp - VirtIO RPMsg Console for MicroPython REPL
 *
 * @impl [SPEC-E906-REPL-03] [SPEC-E906-REPL-05] apps/e906_repl_app/SPECIFICATION.md
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <coroutine>
#include "asp_coro.hpp"
#include "hal/rpmsg.hpp"

namespace abstractx {

class RpmsgConsole {
public:
    static constexpr uint32_t REPL_ENDPOINT_ADDR = 1026;
    static constexpr size_t INPUT_BUFFER_SIZE = 512;
    static constexpr size_t OUTPUT_BUFFER_SIZE = 256;
    static constexpr size_t TX_QUEUE_SIZE = 16;

    explicit RpmsgConsole(::hal::IRpmsg &rpmsg) noexcept;

    // Register RPMsg endpoint and announce service
    // @impl [SPEC-E906-REPL-05] apps/e906_repl_app/SPECIFICATION.md
    void init() noexcept;

    // Feed characters into the input channel (called from RPMsg endpoint callback)
    // @impl [SPEC-E906-REPL-03] apps/e906_repl_app/SPECIFICATION.md
    void on_rx_data(const uint8_t *data, size_t len) noexcept;

    // Direct access to incoming and outgoing AbstractX coroutine channels
    coro::AsyncQueue<char, INPUT_BUFFER_SIZE>& rx_channel() noexcept {
        return m_rx_channel;
    }

    coro::AsyncQueue<char, TX_QUEUE_SIZE>& tx_channel() noexcept {
        return m_tx_channel;
    }

    // Await next character from the input channel
    // @impl [SPEC-E906-REPL-03] apps/e906_repl_app/SPECIFICATION.md
    auto read_char_async() noexcept {
        return m_rx_channel.pop();
    }

    // Check if input characters are available
    [[nodiscard]] bool has_char() const noexcept {
        return !m_rx_channel.is_empty();
    }

    // Try to peek at the next character without removing it from the queue
    // @impl [SPEC-E906-REPL-04] apps/e906_repl_app/SPECIFICATION.md
    bool try_peek_char(char &out_ch) const noexcept {
        return m_rx_channel.try_peek(out_ch);
    }

    // Try to pop a single character without suspending
    bool try_read_char(char &out_ch) noexcept {
        return m_rx_channel.try_pop(out_ch);
    }

    // Send text to the outgoing channel (called by mp_hal_stdout_tx_strn)
    // @impl [SPEC-E906-REPL-05] apps/e906_repl_app/SPECIFICATION.md
    void write(const char *str, size_t len) noexcept;

    // Stage a character into the RPMsg payload buffer
    bool stage_tx_char(char ch) noexcept;
    [[nodiscard]] bool is_tx_buffer_full() const noexcept { return m_out_len >= OUTPUT_BUFFER_SIZE; }

    // Flush staged buffer over RPMsg
    void flush() noexcept;

    // Bytes dropped ([SPEC-E906-REPL-05]): output with no host or no free buffer, input past the queue.
    [[nodiscard]] uint32_t tx_dropped() const noexcept { return m_tx_dropped; }
    [[nodiscard]] uint32_t rx_dropped() const noexcept { return m_rx_dropped; }

    // Flush output that has waited longer than max_age_ticks (mtime): text without a
    // newline (raw-REPL "OK", prompts) must not wait for the script to end ([SPEC-E906-REPL-16]).
    void flush_if_older(uint64_t now_ticks, uint64_t max_age_ticks) noexcept {
        if (m_out_len != 0 && now_ticks - m_out_since >= max_age_ticks) {
            flush();
        }
    }

    // Announce 'abstractx-repl' once the host virtio driver is ready; called on each
    // message box wake. Reading the vrings is interrupt-driven ([SPEC-E906-REPL-16]).
    // @impl [SPEC-E906-REPL-05] [SPEC-E906-REPL-16] apps/e906_repl_app/SPECIFICATION.md
    void service() noexcept {
        if (!m_announced && m_rpmsg.is_driver_ready()) {
            m_announced = m_rpmsg.announce_service("abstractx-repl", REPL_ENDPOINT_ADDR);
        }
    }

private:
    static void rx_callback(const ::hal::RpmsgMessage &msg, void *user_data) noexcept;

    ::hal::IRpmsg &m_rpmsg;
    coro::AsyncQueue<char, INPUT_BUFFER_SIZE> m_rx_channel{};
    coro::AsyncQueue<char, TX_QUEUE_SIZE> m_tx_channel{};
    char m_out_buf[OUTPUT_BUFFER_SIZE]{};
    size_t m_out_len{0};
    uint64_t m_out_since{0};   // mtime when the first unsent byte was staged
    uint32_t m_host_addr{0};
    bool m_host_known{false};
    bool m_announced{false};
    uint32_t m_tx_dropped{0};
    uint32_t m_rx_dropped{0};
};

// Global singleton instance for MicroPython HAL bridging
extern RpmsgConsole *g_repl_console;

} // namespace abstractx
