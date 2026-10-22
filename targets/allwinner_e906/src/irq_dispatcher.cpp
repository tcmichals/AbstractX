#include "hal/crash.hpp"
#include "hal/clic.hpp"
#include "abstractx/trace/runtime_stats.hpp"
#include "abstractx/trace/event_trace.hpp"
#include "abstractx/hal/platform.hpp"

// @impl [SPEC-E906-IRQ-01] targets/allwinner_e906/SPECIFICATION.md

// -----------------------------------------------------------------------------
// Crash Dispatcher Implementation
// -----------------------------------------------------------------------------
extern "C" __attribute__((section(".fastcode"), noinline))
void hal_crash_dispatcher(const hal::CrashFrame* f) noexcept {
    if (f) {
        hal::CrashHandler::handle(*f);
    }
    while (true) {
        __asm__ volatile("wfi");
    }
}

// -----------------------------------------------------------------------------
// Default Fallback Handler (ITCM)
// -----------------------------------------------------------------------------
extern "C" __attribute__((section(".fastcode")))
void default_isr_ignore() noexcept {
    // No-op return
}

// -----------------------------------------------------------------------------
// Weak C-Linkage ISR Declarations (lines with a fixed owner)
// -----------------------------------------------------------------------------
extern "C" {
    void fc_msgbox_doorbell_isr() noexcept __attribute__((weak, alias("default_isr_ignore")));
    void fc_dma_isr()             noexcept __attribute__((weak, alias("default_isr_ignore")));
    void fc_timer_tick_isr()      noexcept __attribute__((weak, alias("default_isr_ignore")));
}

// -----------------------------------------------------------------------------
// CLIC line table ([SPEC-E906-IRQ-01]): fixed lines call their ISR directly,
// other lines run the handlers attached at run time (shared S_INTC group lines
// run each one; every handler checks its own peripheral status).
// -----------------------------------------------------------------------------
// Readable from Linux with devmem (SRAM space 0, PA = DA - 0x38D40000) when the REPL is
// unreachable: [0] idle sleeps, [1] last mcause, [2] interrupts taken, [3] mtvec at boot,
// [4] CLIC word line 17, [5] CLIC word line 7, [6] mie, [7] mstatus (both at the last idle),
// [8] main loop passes, [11] trap entries, [12] raw mcause, [13] mepc of the last trap (startup.S).
extern "C" { volatile uint32_t g_e906_diag[16] = {}; }
extern "C" volatile uint32_t g_irq_sample_only;  // timer.cpp, [SPEC-E906-IRQ-06]

namespace {

struct Slot {
    uint32_t line;
    hal::irq::Handler fn;
    void* ctx;
};

constexpr size_t kMaxSlots = 8;
Slot s_slots[kMaxSlots]{};
size_t s_num_slots = 0;
uint32_t s_counts[hal::clic::kNumLines]{};
uint32_t s_cycles[hal::clic::kNumLines]{};
uint32_t s_spurious = 0;

struct LineInfo {
    uint32_t line;
    const char* name;
    const volatile uint32_t* errors;
};
LineInfo s_info[abstractx::stats::kMaxDrivers + 2]{};
size_t s_num_info = 0;

inline uint32_t mcycle_lo() noexcept {
    uint32_t v = 0;
#if defined(__riscv)
    __asm__ volatile("csrr %0, mcycle" : "=r"(v));
#endif
    return v;
}

} // namespace

namespace hal::irq {

bool attach(uint32_t line, Handler fn, void* ctx) noexcept {
    if (line >= clic::kNumLines || fn == nullptr) return false;
    {
        abstractx::InterruptGuard guard;
        if (s_num_slots >= kMaxSlots) return false;
        s_slots[s_num_slots++] = Slot{line, fn, ctx};
    }
    clic::enable(line);
    return true;
}

uint32_t count(uint32_t line) noexcept {
    return line < clic::kNumLines ? s_counts[line] : 0;
}

uint32_t spurious() noexcept { return s_spurious; }

uint32_t cycles(uint32_t line) noexcept {
    return line < clic::kNumLines ? s_cycles[line] : 0;
}

// @impl [SPEC-E906-IRQ-05] targets/allwinner_e906/SPECIFICATION.md
namespace {
uint32_t s_trap_pc = 0;
}

uint32_t trap_pc() noexcept { return s_trap_pc; }

void describe(uint32_t line, const char* name, const volatile uint32_t* errors) noexcept {
    for (size_t i = 0; i < s_num_info; ++i) {
        if (s_info[i].line == line) {
            s_info[i] = LineInfo{line, name, errors};
            return;
        }
    }
    if (s_num_info < sizeof(s_info) / sizeof(s_info[0])) {
        s_info[s_num_info++] = LineInfo{line, name, errors};
    }
}

} // namespace hal::irq

__attribute__((section(".fastcode")))
static void dispatch_clic_line(uint32_t line) noexcept {
    if (line >= hal::clic::kNumLines) {
        ++s_spurious;
        return;
    }
    ++s_counts[line];
    const uint32_t start = mcycle_lo();
    struct Charge {  // @impl [SPEC-E906-IRQ-05]: handler cycles per line, on every return path
        uint32_t line, start;
        ~Charge() {
            const uint32_t c = mcycle_lo() - start;
            s_cycles[line] += c;
            abstractx::stats::add_isr_cycles(c);
            // @impl [SPEC-TRACE-10] [SPEC-E906-IRQ-06]: one span per handler, except a timer
            // interrupt that only sampled (the profiler shows those)
            if (!g_irq_sample_only) abstractx::events::span(abstractx::events::kIsr, line, start, c);
        }
    } charge{line, start};
    switch (line) {
        case hal::clic::kLineTimer:  fc_timer_tick_isr();      return;
        case hal::clic::kLineMsgbox: fc_msgbox_doorbell_isr(); return;
        case hal::clic::kLineDma:    fc_dma_isr();             return;
        default: break;
    }
    bool handled = false;
    for (size_t i = 0; i < s_num_slots; ++i) {
        if (s_slots[i].line == line) {
            s_slots[i].fn(s_slots[i].ctx);
            handled = true;
        }
    }
    if (!handled) {
        // Nobody owns this line: switch it off instead of re-entering forever.
        hal::clic::disable(line);
        ++s_spurious;
    }
}

// -----------------------------------------------------------------------------
// Top-Level Trap Entry (called by default_trap_entry in startup.S for interrupts)
// -----------------------------------------------------------------------------
extern "C" __attribute__((section(".fastcode")))
void riscv_trap_dispatcher(uint32_t mcause, uint32_t mepc) noexcept {
    hal::irq::s_trap_pc = mepc;  // [SPEC-E906-IRQ-06] for the profiler sample
    constexpr uint32_t INTERRUPT_FLAG = 0x80000000U;
    g_e906_diag[1] = mcause;
    g_e906_diag[2] = g_e906_diag[2] + 1u;
    if (__builtin_expect((mcause & INTERRUPT_FLAG) != 0, 1)) {
        // The E906 mirrors MPP/MPIE/MPIL into mcause[29:16]; the CLIC id is [11:0].
        dispatch_clic_line(mcause & 0xFFFU);
    }
}

// Up to kMaxDrivers lines that fired, most interrupts first ([SPEC-E906-IRQ-05]).
// @impl [SPEC-E906-IRQ-05] targets/allwinner_e906/SPECIFICATION.md
void abstractx::hal::platform_runtime_stats(abstractx::stats::PlatformStats& out) noexcept {
    out.n_drivers = 0;
    bool taken[::hal::clic::kNumLines]{};
    while (out.n_drivers < abstractx::stats::kMaxDrivers) {
        uint32_t best = ::hal::clic::kNumLines;
        for (uint32_t line = 0; line < ::hal::clic::kNumLines; ++line) {
            if (s_counts[line] && !taken[line] && (best == ::hal::clic::kNumLines || s_counts[line] > s_counts[best])) {
                best = line;
            }
        }
        if (best == ::hal::clic::kNumLines) break;
        taken[best] = true;
        auto& d = out.drivers[out.n_drivers++];
        d.id = static_cast<uint16_t>(best);
        d.events = s_counts[best];
        d.cycles = s_cycles[best];
        d.errors = 0;
        d.name = nullptr;
        for (size_t i = 0; i < s_num_info; ++i) {
            if (s_info[i].line == best) {
                d.name = s_info[i].name;
                d.errors = s_info[i].errors ? *s_info[i].errors : 0;
            }
        }
    }
}
