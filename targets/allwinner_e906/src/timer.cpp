#include "timer.hpp"
#include "hal/clic.hpp"
#include "abstractx/isr_dispatcher.hpp"
#include "abstractx/trace/event_trace.hpp"
#include <atomic>
#include <etl/intrusive_list.h>

// Set by a timer interrupt that only took a profiler sample; startup.S reads and clears it
// on trap return and, on the idle wfi, goes back to sleep ([SPEC-E906-IRQ-06]).
extern "C" { volatile uint32_t g_irq_sample_only = 0; }

namespace hal {

// @impl [SPEC-E906-IRQ-03] targets/allwinner_e906/SPECIFICATION.md
// T-Head core timer (measured): mtime 0xE000BFF8, mtimecmp 0xE0004000, 24 MHz.
static constexpr uintptr_t MTIMECMP_BASE = 0xE0004000U;
static constexpr uintptr_t MTIME_BASE    = 0xE000BFF8U;
static constexpr uint64_t  kNever        = ~0ULL;

using WaiterList = etl::intrusive_list<Timer::SleepAwaiter, etl::bidirectional_link<0>>;
static WaiterList g_waiters{};
// Two deadlines share one comparator: sleeping coroutines (always the front of g_waiters) and a
// synchronous wake.
static volatile uint64_t g_coro_target{kNever};  // shared: main under InterruptGuard, timer ISR (IRQs masked); single hart, 64-bit never read unguarded
static volatile uint64_t g_wake_target{kNever};  // shared: arm_wake under InterruptGuard, timer ISR clears; single hart
// Third deadline: profiler samples ([SPEC-E906-IRQ-06], [SPEC-TRACE-09]).
static volatile uint64_t g_sample_target{kNever};  // shared: set_sample_period under InterruptGuard, timer ISR advances; single hart
static volatile uint64_t g_sample_period{0};       // shared: set_sample_period under InterruptGuard, timer ISR reads; single hart

static void program_compare(uint64_t target) noexcept {
    auto *cmp_lo = reinterpret_cast<volatile uint32_t *>(MTIMECMP_BASE);
    auto *cmp_hi = reinterpret_cast<volatile uint32_t *>(MTIMECMP_BASE + 4);
    // High word first to all-ones so the split write never matches early
    *cmp_hi = 0xFFFFFFFFUL;
    if (target == kNever) return;
    *cmp_lo = static_cast<uint32_t>(target & 0xFFFFFFFFUL);
    *cmp_hi = static_cast<uint32_t>(target >> 32);
}

static void reprogram() noexcept {
    const uint64_t a = g_coro_target;
    const uint64_t b = g_wake_target;
    const uint64_t c = g_sample_target;
    const uint64_t ab = a < b ? a : b;
    program_compare(ab < c ? ab : c);
}

void Timer::init() noexcept {
#if defined(__riscv)
    // Enable mcycle/minstret counters (clear mcountinhibit CSR 0x320)
    asm volatile (
        "csrw 0x320, zero\n"
        "csrw 0x306, %0\n"     // mcounteren = 0xFFFFFFFF (allow unprivileged read)
        :: "r"(-1)
    );
#endif
    cancel_alarm();
    // CLIC mode ignores mie.MTIE: the timer is line 7 ([SPEC-E906-IRQ-01]).
    clic::enable(clic::kLineTimer);
#if defined(__riscv)
    // mie.MTIE | mie.MEIE as testFreeRTOS (FreeRTOS sets both) does, then mstatus.MIE
    asm volatile ("csrs mie, %0\n" :: "r"((1U << 7) | (1U << 11)));
    asm volatile ("csrs mstatus, %0\n" :: "r"(1U << 3));
#endif
}

uint64_t Timer::get_ticks() noexcept {
    auto *lo_reg = reinterpret_cast<volatile uint32_t *>(MTIME_BASE);
    auto *hi_reg = reinterpret_cast<volatile uint32_t *>(MTIME_BASE + 4);
    uint32_t hi0 = 0, lo = 0, hi1 = 0;
    do {
        hi0 = *hi_reg;
        lo  = *lo_reg;
        hi1 = *hi_reg;
    } while (hi0 != hi1);
    return (static_cast<uint64_t>(hi0) << 32) | lo;
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

void Timer::cancel_alarm() noexcept {
    abstractx::InterruptGuard guard;
    g_waiters.clear();
    g_coro_target = kNever;
    g_wake_target = kNever;
    reprogram();  // the profiler sample deadline stays
}

// @impl [SPEC-E906-IRQ-03] targets/allwinner_e906/SPECIFICATION.md
void Timer::schedule_waiter(SleepAwaiter* node) noexcept {
    if (node == nullptr) return;
    abstractx::InterruptGuard guard;
    if (node->is_linked()) {
        g_waiters.erase(*node);
    }
    auto it = g_waiters.begin();
    while (it != g_waiters.end() && it->target_ticks <= node->target_ticks) {
        ++it;
    }
    g_waiters.insert(it, *node);
    if (g_waiters.front().target_ticks < g_coro_target) {
        g_coro_target = g_waiters.front().target_ticks;
    }
    reprogram();
}

void Timer::cancel_waiter(SleepAwaiter* node) noexcept {
    if (node == nullptr) return;
    abstractx::InterruptGuard guard;
    if (!node->is_linked()) return;
    const bool was_front = (&g_waiters.front() == node);
    g_waiters.erase(*node);
    if (was_front) {
        g_coro_target = !g_waiters.empty() ? g_waiters.front().target_ticks : kNever;
        reprogram();
    }
}

// @impl [SPEC-E906-IRQ-06] targets/allwinner_e906/SPECIFICATION.md
void Timer::set_sample_period(uint64_t ticks) noexcept {
    abstractx::InterruptGuard guard;
    g_sample_period = ticks;
    g_sample_target = ticks ? get_ticks() + ticks : kNever;
    reprogram();
}

void Timer::arm_wake(uint64_t target_ticks) noexcept {
    abstractx::InterruptGuard guard;
    if (target_ticks < g_wake_target || g_wake_target <= get_ticks()) {
        g_wake_target = target_ticks;
    }
    reprogram();
}

// Line 7 is level while mtime >= mtimecmp: always leave the comparator in the future or parked.
void Timer::handle_irq() noexcept {
    const uint64_t now = get_ticks();
    bool other = false;
    // @impl [SPEC-E906-IRQ-06] targets/allwinner_e906/SPECIFICATION.md
    if (now >= g_sample_target) {
        abstractx::events::sample(irq::trap_pc());
        const uint64_t next = g_sample_target + g_sample_period;
        g_sample_target = next > now ? next : now + g_sample_period;  // fell behind: skip
    }
    while (!g_waiters.empty() && now >= g_waiters.front().target_ticks) {
        other = true;
        SleepAwaiter& w = g_waiters.front();
        g_waiters.pop_front();
        const auto h = w.handle;
        const auto cb = w.callback;
        void* const c = w.ctx;
        if (cb != nullptr) {
            cb(c);
        }
        if (h) {
            abstractx::IsrDispatcher::post(h);
        }
    }
    g_coro_target = !g_waiters.empty() ? g_waiters.front().target_ticks : kNever;
    if (now >= g_wake_target) {
        other = true;
        g_wake_target = kNever;  // the woken wfi loop re-checks its own deadline
    }
    g_irq_sample_only = other ? 0u : 1u;
    reprogram();
}

// ----------------------------------------------------------------------------
// Non-Blocking Coroutine Sleep Implementation
// @impl [SPEC-E906-IRQ-03] targets/allwinner_e906/SPECIFICATION.md
// ----------------------------------------------------------------------------
Timer::SleepAwaiter::~SleepAwaiter() noexcept {
    Timer::cancel_waiter(this);
}

bool Timer::SleepAwaiter::await_ready() const noexcept {
    return Timer::get_ticks() >= target_ticks;
}

bool Timer::SleepAwaiter::await_suspend(std::coroutine_handle<> h) noexcept {
    if (Timer::get_ticks() >= target_ticks) {
        return false;
    }
    handle = h;
    Timer::schedule_waiter(this);
    return true;
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
    arm_wake(end);  // guarantees a timer interrupt to end the wfi
    while (get_ticks() < end) {
#if defined(__riscv)
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