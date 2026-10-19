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
#include <cstdint>

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

void mp_hal_delay_ms(mp_uint_t ms) {
    uint64_t start = read_mcycle();
    uint64_t target = static_cast<uint64_t>(ms) * 200000ULL;
    while ((read_mcycle() - start) < target) {
        abstractx_repl_vm_hook();
    }
}

void mp_hal_delay_us(mp_uint_t us) {
    uint64_t start = read_mcycle();
    uint64_t target = static_cast<uint64_t>(us) * 200ULL;
    while ((read_mcycle() - start) < target) {
        abstractx_repl_vm_hook();
    }
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
// @impl [SPEC-E906-REPL-04] apps/e906_repl_app/SPECIFICATION.md
void abstractx_repl_vm_hook(void) {
    // Cooperatively pump the dispatcher
    abstractx::Dispatcher::process();
}

// Garbage collection root scan
// @impl [SPEC-E906-REPL-02] apps/e906_repl_app/SPECIFICATION.md
void gc_collect(void) {
    gc_collect_start();
    void *dummy;
    gc_collect_root(&dummy, ((mp_uint_t)MP_STATE_THREAD(stack_top) - (mp_uint_t)&dummy) / sizeof(mp_uint_t));
    gc_collect_end();
}

// Trap handler on uncaught longjmp failure
void nlr_jump_fail(void *val) {
    (void)val;
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

// Polling character read for raw/event REPL helpers
int mp_hal_stdin_rx_chr(void) {
    if (abstractx::g_repl_console) {
        char ch = 0;
        while (!abstractx::g_repl_console->try_read_char(ch)) {
            abstractx_repl_vm_hook();
        }
        return static_cast<int>(ch);
    }
    return 0;
}

} // extern "C"

