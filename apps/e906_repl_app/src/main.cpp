/*
 * main.cpp - XuanTie E906 MicroPython REPL Application
 *
 * @impl [SPEC-E906-REPL-01] [SPEC-E906-REPL-02] [SPEC-E906-REPL-03] [SPEC-E906-REPL-07] [SPEC-E906-REPL-08] [SPEC-E906-REPL-12] [SPEC-E906-REPL-16] apps/e906_repl_app/SPECIFICATION.md
 */

#include "abstractx/abstractx.hpp"
#include "asp_coro.hpp"
#include "abstractx/hal/platform.hpp"
#include "hal/rpmsg.hpp"
#include "hal/clic.hpp"
#include "memory_map.h"
#include "rpmsg_console.hpp"

extern "C" {
#include "py/runtime.h"
#include "py/gc.h"
#include "py/stackctrl.h"
#include "shared/runtime/pyexec.h"
#include "shared/runtime/interrupt_char.h"
}

namespace abstractx::hal {
extern ::hal::Rpmsg g_rpmsg;
}

// Statically allocated Console instance bound to unified g_rpmsg (Zero Heap)
static abstractx::RpmsgConsole s_console(abstractx::hal::g_rpmsg);

// 1. RPMsg RX: woken by the message box interrupt ([SPEC-E906-IRQ-04]); each wake
//    also retries the 'abstractx-repl' announcement until the host is ready.
static abstractx::coro::Task<void> rpmsg_rx_coroutine(::hal::IRpmsg &rpmsg, abstractx::RpmsgConsole &console) {
    for (;;) {
        console.service();
        co_await rpmsg.async_receive();
    }
}

// 2. Cooperative C++20 MicroPython REPL Input Task (Awaits incoming character channel)
// @impl [SPEC-E906-REPL-03] apps/e906_repl_app/SPECIFICATION.md
static abstractx::coro::Task<void> repl_rx_coroutine(abstractx::RpmsgConsole &console) {
    for (;;) {
        char ch = co_await console.read_char_async();
        int ret = pyexec_event_repl_process_char(ch);
        if (!console.has_char()) {
            console.flush();  // a reply without '\n' (raw-REPL "\x04\x04>") goes out now
        }
        if (ret & PYEXEC_FORCED_EXIT) {
            mp_deinit();
            char stack_dummy;
            mp_stack_set_top(&stack_dummy);
            mp_stack_set_limit(8192);
            mp_init();
            pyexec_event_repl_init();
        }
    }
}

// 3. Outgoing C++20 Coroutine Task for REPL Output Streaming (Awaits outgoing channel)
// @impl [SPEC-E906-REPL-05] apps/e906_repl_app/SPECIFICATION.md
static abstractx::coro::Task<void> repl_tx_coroutine(abstractx::RpmsgConsole &console) {
    for (;;) {
        // Cooperatively await the next outgoing character from MicroPython stdout channel
        char ch = co_await console.tx_channel().pop();
        console.stage_tx_char(ch);

        // Opportunistically batch any pending characters up to buffer size
        while (!console.is_tx_buffer_full() && console.tx_channel().try_pop(ch)) {
            console.stage_tx_char(ch);
        }
        console.flush();
    }
}

#include "hal/trace.hpp"

extern "C" {
extern const struct rpmsg_resource_table global_resource_table;
}

int main() {
    // 1. Initialize AbstractX Master Runtime & Trace buffer (Liveness & Crash only)
    hal::Trace::init(false);
    hal::Trace::puts("[E906] Online\n");

    abstractx::Config config{};
    abstractx::init(config);

    // 2. Initialize Console on shared RPMsg bus
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

    // 5. Spawn cooperative C++20 coroutines
    // @impl [SPEC-E906-REPL-18] apps/e906_repl_app/SPECIFICATION.md
    abstractx::spawn(rpmsg_rx_coroutine(abstractx::hal::g_rpmsg, s_console), "rpmsg_rx");
    abstractx::spawn(repl_rx_coroutine(s_console), "repl_rx");
    abstractx::spawn(repl_tx_coroutine(s_console), "repl_tx");

    // 6. Cooperative Event Loop: interrupts post work; sleep (wfi) when nothing is ready
    for (;;) {
        g_e906_diag[8] = g_e906_diag[8] + 1u;  // loop count, readable over devmem
        abstractx::step();
        if (!abstractx::Dispatcher::has_pending_resumes()) {
            abstractx::hal::platform_idle_wait();
        }
    }

    return 0;
}
