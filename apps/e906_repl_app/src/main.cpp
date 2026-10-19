/*
 * main.cpp - XuanTie E906 MicroPython REPL Application
 *
 * @impl [SPEC-E906-REPL-01] [SPEC-E906-REPL-02] [SPEC-E906-REPL-03] apps/e906_repl_app/SPECIFICATION.md
 */

#include "abstractx/abstractx.hpp"
#include "asp_coro.hpp"
#include "hal/rpmsg.hpp"
#include "memory_map.h"
#include "rpmsg_console.hpp"

extern "C" {
#include "py/runtime.h"
#include "py/gc.h"
#include "py/stackctrl.h"
#include "shared/runtime/pyexec.h"
}

// Statically allocated RPMsg and Console instances (Zero Heap)
static ::hal::Rpmsg s_rpmsg;
static abstractx::RpmsgConsole s_console(s_rpmsg);

// Cooperative C++20 MicroPython REPL Task
// @impl [SPEC-E906-REPL-03] apps/e906_repl_app/SPECIFICATION.md
static abstractx::coro::Task<void> repl_coroutine(abstractx::RpmsgConsole &console) {
    for (;;) {
        char ch = co_await console.read_char_async();
        if (ch != 0) {
            int ret = pyexec_event_repl_process_char(ch);
            if (ret & PYEXEC_FORCED_EXIT) {
                mp_deinit();
                char stack_dummy;
                mp_stack_set_top(&stack_dummy);
                mp_stack_set_limit(8192);
                mp_init();
                pyexec_event_repl_init();
            }
            console.flush();
        }
    }
}

int main() {
    // 1. Initialize AbstractX Master Runtime
    abstractx::Config config{};
    abstractx::init(config);

    // 2. Initialize VirtIO RPMsg Transport and Console
    s_rpmsg.init(nullptr);
    s_console.init();

    // 3. Initialize MicroPython GC Heap quarantined in dormant HiFi4 DSP SRAM
    // @impl [SPEC-E906-REPL-02] apps/e906_repl_app/SPECIFICATION.md
    void *dsp_heap_start = reinterpret_cast<void *>(DSP_LOCAL_RAM_BASE);
    void *dsp_heap_end = reinterpret_cast<void *>(DSP_LOCAL_RAM_BASE + DSP_LOCAL_RAM_SIZE);
    gc_init(dsp_heap_start, dsp_heap_end);

    // 4. Initialize MicroPython VM & Event-Driven REPL
    char stack_dummy;
    mp_stack_set_top(&stack_dummy);
    mp_stack_set_limit(8192);
    mp_init();
    pyexec_event_repl_init();

    // 5. Spawn cooperative C++20 REPL coroutine
    abstractx::spawn(repl_coroutine(s_console));

    // 6. Cooperative Event Loop
    for (;;) {
        abstractx::step();
    }

    return 0;
}
