#ifndef IOPROCESSOR_HAL_SPI_HPP
#define IOPROCESSOR_HAL_SPI_HPP
// @impl [SPEC-E906-HAL-01] targets/allwinner_e906/SPECIFICATION.md
// @impl [SPEC-E906-HAL-03] targets/allwinner_e906/SPECIFICATION.md

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <coroutine>
#include <etl/delegate.h>

namespace fc::hal {

enum class SpiMode {
    SingleFullDuplex,
    DualIO,
};

class Spi1 {
public:
    // Threshold balancing ([SPEC-E906-HAL-01], [SPEC-E906-HAL-03]): transfers <= 64 bytes (SPI1 hardware FIFO depth)
    // use direct fast-path FIFO avoiding DMA channel allocation & setup latency;
    // transfers > 64 bytes (IMU bursts, FPGA frames) stream via dual-channel DMA.
    static constexpr size_t DMA_THRESHOLD = 64;

    static void init(uint32_t speed_hz = 10000000);
    static void set_speed(uint32_t speed_hz);
    
    /* Low-Latency Fast FIFO Direct Transfer (Commands & Short Register Read/Write) */
    static bool transceive_fifo_sync(int cs_id, const uint8_t *tx_buf, uint8_t *rx_buf, size_t length, bool dual = false);

    /* Synchronous Fallback Transfers */
    static bool transceive_fpga_dual_sync(const uint8_t *tx_buf, uint8_t *rx_buf, size_t length);
    static bool transceive_imu_single_sync(const uint8_t *tx_buf, uint8_t *rx_buf, size_t length);

    /* Asynchronous Non-Blocking DMA Transfers (Sunxi DMA Engine, DRQ_SPI1 = 23) */
    static bool start_dma_transfer(
        int cs_id,
        const uint8_t *tx,
        uint8_t *rx,
        size_t len,
        etl::delegate<void(bool)> callback = {},
        bool dual = false
    ) noexcept;

    static bool is_busy() noexcept;

    /* Asynchronous Non-Blocking Coroutine Transfers (Threshold-Balanced) */
    struct AsyncTransferAwaiter {
        SpiMode mode;
        int cs_id;
        const uint8_t *tx;
        uint8_t *rx;
        size_t len;
        bool completed{false};

        AsyncTransferAwaiter(SpiMode m, int cs, const uint8_t *t, uint8_t *r, size_t l);
        bool await_ready() const noexcept;
        bool await_suspend(std::coroutine_handle<> handle) noexcept;
        bool await_resume() noexcept;
    };

    static inline AsyncTransferAwaiter async_transceive_fpga_dual(const uint8_t *tx, uint8_t *rx, size_t len) {
        return AsyncTransferAwaiter(SpiMode::DualIO, 0 /* CS0: PB11 */, tx, rx, len);
    }

    static inline AsyncTransferAwaiter async_transceive_imu_single(const uint8_t *tx, uint8_t *rx, size_t len) {
        return AsyncTransferAwaiter(SpiMode::SingleFullDuplex, 0 /* CS0: PB11 */, tx, rx, len);
    }

    /* PLIC/CLIC Interrupt Service Routine Handler */
    static void handle_irq();

private:
    static bool start_async_transfer(SpiMode mode, int cs_id, const uint8_t *tx, uint8_t *rx, size_t len);
};

// Backwards-compatibility alias for hardware SPI
using Spi0 = Spi1;

} // namespace fc::hal

#endif // IOPROCESSOR_HAL_SPI_HPP
