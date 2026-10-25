/*
 * mphalport.cpp - MicroPython HAL and VM Hooks for XuanTie E906
 *
 * @impl [SPEC-E906-REPL-02] [SPEC-E906-REPL-04] apps/e906_repl_app/SPECIFICATION.md
 */

extern "C" {
#include "py/mphal.h"
#include "py/mpstate.h"
#include "py/gc.h"
#include "py/lexer.h"
#include "py/builtin.h"
#include "py/runtime.h"
#include "py/mperrno.h"
}

#include "mpconfigport.h"
#include "abstractx/abstractx.hpp"
#include "asp_coro.hpp"
#include "rpmsg_console.hpp"
#include "abstractx/hal/platform.hpp"
#include "hal/timer.hpp"
#include "hal/rpmsg.hpp"
#include "hal/rpmsg_endpoints.hpp"
#include "tlp_ring.hpp"
#include <cstdint>

namespace abstractx::hal {
extern volatile uint32_t g_telemetry_dropped;
extern ::hal::RpmsgTlpChannel g_tlp_channel;
}

namespace {
__attribute__((section(".sram_c"))) abstractx::MpscTlpRing<> s_tlp_ingress_ring;
bool s_tlp_ingress_attached = false;
} // namespace

#if defined(__riscv)
static inline uint64_t read_mcycle() noexcept {
    uint32_t hi0, hi1, lo;
    do {
        asm volatile("csrr %0, mcycleh" : "=r"(hi0));
        asm volatile("csrr %0, mcycle"  : "=r"(lo));
        asm volatile("csrr %0, mcycleh" : "=r"(hi1));
    } while (hi0 != hi1);
    return (static_cast<uint64_t>(hi0) << 32) | lo;
}
#else
static inline uint64_t read_mcycle() noexcept {
    return 0;
}
#endif

extern "C" {

// Sleeps use wfi with a timer interrupt at the deadline ([SPEC-E906-REPL-16]).
static void sleep_until_ticks(uint64_t deadline) {
    while (hal::Timer::get_ticks() < deadline) {
        hal::Timer::arm_wake(deadline);
        abstractx_repl_vm_hook();
        abstractx::hal::platform_idle_wait();
        abstractx::status_service();  // [SPEC-E906-REPL-18]
    }
}

void mp_hal_delay_ms(mp_uint_t ms) {
    sleep_until_ticks(hal::Timer::get_ticks() + static_cast<uint64_t>(ms) * hal::Timer::TICKS_PER_MS);
}

void mp_hal_delay_us(mp_uint_t us) {
    sleep_until_ticks(hal::Timer::get_ticks() + static_cast<uint64_t>(us) * hal::Timer::TICKS_PER_US);
}

mp_uint_t mp_hal_ticks_ms(void) {
    return static_cast<mp_uint_t>(read_mcycle() / 200000ULL);
}

mp_uint_t mp_hal_ticks_us(void) {
    return static_cast<mp_uint_t>(read_mcycle() / 200ULL);
}

mp_uint_t mp_hal_ticks_cpu(void) {
    return static_cast<mp_uint_t>(read_mcycle());
}

// Non-preemptive VM Hook: called on every bytecode loop iteration
// @impl [SPEC-E906-REPL-04] [SPEC-E906-REPL-16] apps/e906_repl_app/SPECIFICATION.md
void abstractx_repl_vm_hook(void) {
    static bool s_running = false;
    if (s_running) {
        return;
    }
    s_running = true;

    // Runs on every backward jump: return at once unless an interrupt posted work,
    // or every 64th call (aged output, TLP batches).
    static uint32_t s_calls = 0;
    if (!::hal::rpmsg_doorbell_pending() && !abstractx::Dispatcher::has_pending_resumes() &&
        (++s_calls & 63U) != 0) {
        s_running = false;
        return;
    }
    // Cooperatively pump the dispatcher
    abstractx::Dispatcher::process();
    // Host messages (Ctrl-C) are read only after a message box interrupt; this also
    // flushes partial TLP batches so tlp_send() data leaves during long scripts.
    abstractx::hal::platform_poll_network();
    abstractx::status_service();  // while a script runs, step() does not ([SPEC-E906-REPL-18])

    if (abstractx::g_repl_console) {
        abstractx::g_repl_console->flush_if_older(hal::Timer::get_ticks(), 20ULL * hal::Timer::TICKS_PER_MS);
    }
    // Clear reentrancy guard BEFORE mp_handle_pending so an NLR longjmp (KeyboardInterrupt)
    // does not leave s_running stuck as true!
    s_running = false;
    mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
}

// Garbage collection root scan: spill RISC-V callee-saved registers (s0..s11) onto the stack
// before scanning from `regs` to `stack_top` so live pointers in registers are never swept.
// @impl [SPEC-E906-REPL-02] apps/e906_repl_app/SPECIFICATION.md
void gc_collect(void) {
    gc_collect_start();
    uintptr_t regs[12];
#if defined(__riscv)
    asm volatile(
        "sw s0,   0(%0)\n"
        "sw s1,   4(%0)\n"
        "sw s2,   8(%0)\n"
        "sw s3,  12(%0)\n"
        "sw s4,  16(%0)\n"
        "sw s5,  20(%0)\n"
        "sw s6,  24(%0)\n"
        "sw s7,  28(%0)\n"
        "sw s8,  32(%0)\n"
        "sw s9,  36(%0)\n"
        "sw s10, 40(%0)\n"
        "sw s11, 44(%0)\n"
        :
        : "r"(regs)
        : "memory"
    );
#else
    for (size_t i = 0; i < 12; ++i) {
        regs[i] = 0;
    }
#endif
    gc_collect_root(reinterpret_cast<void **>(regs),
                    ((mp_uint_t)MP_STATE_THREAD(stack_top) - (mp_uint_t)regs) / sizeof(mp_uint_t));
    gc_collect_end();
}

// Trap handler on uncaught longjmp failure
// @impl [SPEC-E906-REPL-04] apps/e906_repl_app/SPECIFICATION.md
void nlr_jump_fail(void *val) {
    mp_print_str(&mp_plat_print, "\r\nFATAL: uncaught MicroPython exception (nlr_jump_fail)!\r\n");
    if (val != nullptr) {
        mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(val));
    }
    if (abstractx::g_repl_console) {
        abstractx::g_repl_console->flush();
    }
    for (;;) {
#if defined(__riscv)
        asm volatile("wfi");
#endif
    }
}

// Filesystem-less lexer and import stubs
mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    (void)filename;
    mp_raise_OSError(MP_ENOENT);
}

mp_import_stat_t mp_import_stat(const char *path) {
    (void)path;
    return MP_IMPORT_STAT_NO_EXIST;
}

// Blocking character read for raw/event REPL helpers: sleeps (wfi) until RX arrives
int mp_hal_stdin_rx_chr(void) {
    if (abstractx::g_repl_console) {
        abstractx::g_repl_console->flush();
        char ch = 0;
        while (!abstractx::g_repl_console->try_read_char(ch)) {
            abstractx_repl_vm_hook();
            abstractx::hal::platform_idle_wait();
        }
        return static_cast<int>(ch);
    }
    return 0;
}

size_t abstractx_console_rx_bytes(uint8_t *dest, size_t max_len) {
    if (!abstractx::g_repl_console || !dest || max_len == 0) return 0;
    size_t count = 0;
    char ch = 0;
    while (count < max_len && abstractx::g_repl_console->try_read_char(ch)) {
        dest[count++] = static_cast<uint8_t>(ch);
    }
    return count;
}

bool abstractx_console_has_rx(void) {
    return abstractx::g_repl_console ? abstractx::g_repl_console->has_char() : false;
}

// @impl [SPEC-E906-REPL-10] apps/e906_repl_app/SPECIFICATION.md
bool abstractx_tlp_send_bytes(const uint8_t *data, size_t len) {
    if (!data || len != 64) return false;
    abstractx::hal::platform_send_telemetry(data, len);
    return true;
}

// @impl [SPEC-E906-REPL-10] apps/e906_repl_app/SPECIFICATION.md
void abstractx_tlp_ingress_init(void) {
    if (!s_tlp_ingress_attached) {
        s_tlp_ingress_ring.clear();
        abstractx::hal::g_tlp_channel.set_ingress_ring(&s_tlp_ingress_ring);
        s_tlp_ingress_attached = true;
    }
}

// @impl [SPEC-E906-REPL-10] apps/e906_repl_app/SPECIFICATION.md
bool abstractx_tlp_poll_bytes(uint8_t *dst, size_t len) {
    if (!dst || len != 64) return false;
    abstractx_tlp_ingress_init();
    abstractx::Tlp64 tlp{};
    if (!s_tlp_ingress_ring.pop(tlp)) {
        return false;
    }
    const auto *src = reinterpret_cast<const uint8_t *>(&tlp.wire);
    for (size_t i = 0; i < 64; ++i) {
        dst[i] = src[i];
    }
    return true;
}

// @impl [SPEC-E906-REPL-07] apps/e906_repl_app/SPECIFICATION.md
void abstractx_trace_emit(uint32_t task_id, uint8_t state, const char *msg) {
    uint64_t now_us = mp_hal_ticks_us();
    abstractx::trace::g_tracer.trace_coro_named(
        task_id, 0, static_cast<abstractx::trace::CoroState>(state), 0,
        msg ? msg : "mpy", "repl", now_us
    );
    abstractx::trace::g_tracer.flush(now_us);
}

// @impl [SPEC-E906-REPL-10] apps/e906_repl_app/SPECIFICATION.md
uint32_t abstractx_tlp_dropped_count(void) {
    return abstractx::hal::g_telemetry_dropped;
}

} // extern "C"

