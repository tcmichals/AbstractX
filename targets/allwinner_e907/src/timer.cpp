#include "timer.hpp"
#include "abstractx/isr_dispatcher.hpp"
#include <atomic>

namespace hal {

// CORET / CLINT Register Definitions
static constexpr uintptr_t CORET_CMP_BASE   = 0x14004000;

static std::atomic<std::coroutine_handle<>> g_timer_coroutine{nullptr};
static uint64_t g_alarm_target_ticks{0};
static volatile bool g_alarm_armed{false};

void Timer::init() noexcept {
#if defined(__riscv)
    // 1. Enable mcycle/minstret counters (clear mcountinhibit CSR 0x320)
    asm volatile (
        "csrw 0x320, zero\n"   // allow all counters to increment
        "csrw 0x306, %0\n"     // mcounteren = 0xFFFFFFFF (allow unprivileged read)
        :: "r"(-1)
    );

    // 2. Enable Machine Timer Interrupt (MTIE = bit 7 in mie CSR)
    uint32_t mtie = (1U << 7);
    asm volatile (
        "csrs mie, %0\n"
        :: "r"(mtie)
    );

    // 3. Enable Global Interrupts (MIE = bit 3 in mstatus CSR)
    uint32_t mie_global = (1U << 3);
    asm volatile (
        "csrs mstatus, %0\n"
        :: "r"(mie_global)
    );
#endif
    cancel_alarm();
}

uint64_t Timer::get_ticks() noexcept {
#if defined(__riscv)
    // 64-bit atomic counter read using rdcycle / rdcycleh (runs at 24 MHz on Allwinner)
    uint32_t hi0 = 0, lo = 0, hi1 = 0;
    do {
        asm volatile (
            "1: rdcycleh %0\n"
            "   rdcycle  %1\n"
            "   rdcycleh %2\n"
            : "=r"(hi0), "=r"(lo), "=r"(hi1)
            :
            : "memory"
        );
    } while (hi0 != hi1);

    return (static_cast<uint64_t>(hi0) << 32) | lo;
#else
    return 0;
#endif
}

uint64_t Timer::get_time_us() noexcept {
    return get_ticks() / TICKS_PER_US;
}

uint64_t Timer::get_time_ns() noexcept {
    return (get_ticks() * 125ULL) / 3ULL;
}

uint32_t Timer::get_time_ms() noexcept {
    return static_cast<uint32_t>(get_ticks() / TICKS_PER_MS);
}

void Timer::set_compare(uint64_t target_ticks) noexcept {
    {
        abstractx::InterruptGuard guard;
        g_alarm_target_ticks = target_ticks;
        g_alarm_armed = true;
    }

    auto *cmp_lo = reinterpret_cast<volatile uint32_t *>(CORET_CMP_BASE);
    auto *cmp_hi = reinterpret_cast<volatile uint32_t *>(CORET_CMP_BASE + 4);

    // Prevent spurious match during split 32-bit writes
    *cmp_hi = 0xFFFFFFFFUL;
    *cmp_lo = static_cast<uint32_t>(target_ticks & 0xFFFFFFFFUL);
    *cmp_hi = static_cast<uint32_t>(target_ticks >> 32);

#if defined(__riscv)
    uint32_t mtie = (1U << 7);
    asm volatile ("csrs mie, %0\n" :: "r"(mtie));
#endif
}

void Timer::set_alarm_us(uint32_t us_from_now) noexcept {
    set_compare(get_ticks() + (static_cast<uint64_t>(us_from_now) * TICKS_PER_US));
}

void Timer::cancel_alarm() noexcept {
    abstractx::InterruptGuard guard;
    g_alarm_armed = false;
    auto *cmp_hi = reinterpret_cast<volatile uint32_t *>(CORET_CMP_BASE + 4);
    *cmp_hi = 0xFFFFFFFFUL;
}

void Timer::handle_irq() noexcept {
    if (!g_alarm_armed) {
        return;
    }

    uint64_t now = get_ticks();
    if (now >= g_alarm_target_ticks) {
        g_alarm_armed = false;
        auto *cmp_hi = reinterpret_cast<volatile uint32_t *>(CORET_CMP_BASE + 4);
        *cmp_hi = 0xFFFFFFFFUL;

        auto handle = g_timer_coroutine.exchange(nullptr, std::memory_order_acq_rel);
        if (handle) {
            abstractx::IsrDispatcher::post(handle);
        }
    }
}

// ----------------------------------------------------------------------------
// Non-Blocking Coroutine Sleep Implementation
// ----------------------------------------------------------------------------
bool Timer::SleepAwaiter::await_ready() const noexcept {
    return Timer::get_ticks() >= target_ticks;
}

void Timer::SleepAwaiter::await_suspend(std::coroutine_handle<> h) noexcept {
    g_timer_coroutine.store(h, std::memory_order_release);
    Timer::set_compare(target_ticks);
}

Timer::SleepAwaiter Timer::sleep_ticks_async(uint64_t ticks) noexcept {
    return SleepAwaiter{ Timer::get_ticks() + ticks };
}

Timer::SleepAwaiter Timer::sleep_us_async(uint32_t us) noexcept {
    return sleep_ticks_async(static_cast<uint64_t>(us) * TICKS_PER_US);
}

Timer::SleepAwaiter Timer::sleep_ms_async(uint32_t ms) noexcept {
    return sleep_ticks_async(static_cast<uint64_t>(ms) * TICKS_PER_MS);
}

// ----------------------------------------------------------------------------
// Early-Boot or Bounded Synchronous Delays (Low-power, zero busy spinning)
// ----------------------------------------------------------------------------
void Timer::delay_ticks(uint64_t ticks) noexcept {
    uint64_t end = get_ticks() + ticks;
    while (get_ticks() < end) {
#if defined(__riscv)
        // If interrupts are enabled, halt core in low-power WFI until next IRQ
        asm volatile ("wfi");
#endif
    }
}

void Timer::delay_us(uint32_t us) noexcept {
    delay_ticks(static_cast<uint64_t>(us) * TICKS_PER_US);
}

void Timer::delay_ms(uint32_t ms) noexcept {
    delay_ticks(static_cast<uint64_t>(ms) * TICKS_PER_MS);
}

} // namespace hal

extern "C" void fc_timer_tick_isr() noexcept {
    hal::Timer::handle_irq();
}