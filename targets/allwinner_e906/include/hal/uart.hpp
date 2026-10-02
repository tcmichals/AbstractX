#ifndef IOPROCESSOR_HAL_UART_HPP
#define IOPROCESSOR_HAL_UART_HPP

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <coroutine>
#include <etl/delegate.h>

namespace fc::hal {

class Uart2 {
public:
    // Threshold balancing:
    // Writes <= 32 bytes or single characters use direct FIFO / SPSC ring buffer (no DMA overhead)
    // Writes > 32 bytes stream via hardware DMA Channel (DRQ 16)
    static constexpr size_t DMA_TX_THRESHOLD = 32;

    static void init(uint32_t baud_rate = 115200, uint32_t apb_clock_hz = 24000000);
    static void set_baud(uint32_t baud_rate, uint32_t apb_clock_hz = 24000000);

    /* Direct Transmission (Single byte / small buffer) */
    static void write_byte(uint8_t ch);
    static void write(const uint8_t *data, size_t len);
    static void write_str(const char *str);

    /* Asynchronous Zero-Copy DMA Transmission for large bursts (> 32 bytes) */
    static bool write_async_dma(const uint8_t *data, size_t len, etl::delegate<void(bool)> callback = {}) noexcept;
    static bool is_tx_busy() noexcept;

    /* C++20 Coroutine Async TX Awaiter with Threshold Balancing */
    struct AsyncTxAwaiter {
        const uint8_t *data;
        size_t len;
        bool completed{false};

        AsyncTxAwaiter(const uint8_t *d, size_t l) : data(d), len(l) {}
        bool await_ready() const noexcept { return len == 0; }
        void await_suspend(std::coroutine_handle<> handle) noexcept;
        bool await_resume() noexcept { return completed; }
    };

    static inline AsyncTxAwaiter async_write(const uint8_t *data, size_t len) {
        return AsyncTxAwaiter(data, len);
    }

    /* Asynchronous Non-Blocking Coroutine Packet Ingestion (RTO Driven) */
    struct AsyncRxPacketAwaiter {
        uint8_t *dest_buf;
        size_t   max_len;
        size_t   bytes_received;

        AsyncRxPacketAwaiter(uint8_t *buf, size_t len);
        bool await_ready() const noexcept;
        void await_suspend(std::coroutine_handle<> handle) noexcept;
        size_t await_resume() noexcept;
    };

    static inline AsyncRxPacketAwaiter async_read_packet(uint8_t *buf, size_t max_len) {
        return AsyncRxPacketAwaiter(buf, max_len);
    }

    /* PLIC Interrupt Service Routine Handler (Called on IRQ 10) */
    static void handle_irq();

    /* Direct Status */
    static bool has_data();
    static uint8_t read_byte();
};

} // namespace fc::hal

#endif // IOPROCESSOR_HAL_UART_HPP
