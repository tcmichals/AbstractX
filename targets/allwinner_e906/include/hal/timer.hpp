#pragma once

#include <stdint.h>
#include <coroutine>

namespace hal {

class Timer {
public:
    // Allwinner Architectural Counter frequency is 24 MHz (24 ticks per microsecond)
    static constexpr uint64_t CLOCK_FREQ_HZ = 24'000'000ULL;
    static constexpr uint64_t TICKS_PER_US   = 24ULL;
    static constexpr uint64_t TICKS_PER_MS   = 24'000ULL;

    static void init() noexcept;

    // Monotonic timestamp functions (64-bit atomic counter read, never wraps in normal lifetimes)
    static uint64_t get_ticks() noexcept;
    static uint64_t get_time_us() noexcept;
    static uint64_t get_time_ns() noexcept;
    static uint32_t get_time_ms() noexcept;

    // Hardware alarm configuration (CLINT / Core Timer comparator)
    static void set_compare(uint64_t target_ticks) noexcept;
    static void set_alarm_us(uint32_t us_from_now) noexcept;
    static void cancel_alarm() noexcept;

    // Top-half ISR called from riscv_trap_dispatcher on MTIP (mcause 7)
    static void handle_irq() noexcept;

    // ------------------------------------------------------------------------
    // Non-Blocking Coroutine Sleep Awaiters (ZERO POLLING / ZERO HARD LOOPS)
    // ------------------------------------------------------------------------
    struct SleepAwaiter {
        uint64_t target_ticks;

        bool await_ready() const noexcept;
        void await_suspend(std::coroutine_handle<> h) noexcept;
        void await_resume() noexcept {}
    };

    static SleepAwaiter sleep_ticks_async(uint64_t ticks) noexcept;
    static SleepAwaiter sleep_us_async(uint32_t us) noexcept;
    static SleepAwaiter sleep_ms_async(uint32_t ms) noexcept;

    // Early-boot or bounded synchronous delays (uses low-power WFI when interrupts enabled)
    static void delay_ticks(uint64_t ticks) noexcept;
    static void delay_us(uint32_t us) noexcept;
    static void delay_ms(uint32_t ms) noexcept;
};

} // namespace hal