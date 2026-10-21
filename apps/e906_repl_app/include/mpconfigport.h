/*
 * mpconfigport.h - MicroPython configuration for AbstractX E906 (Allwinner A5E / A7A)
 *
 * @impl [SPEC-E906-REPL-02] [SPEC-E906-REPL-04] [SPEC-E906-REPL-06] apps/e906_repl_app/SPECIFICATION.md
 */

#pragma once

#include <stdint.h>
#include <alloca.h>

#ifdef __cplusplus
extern "C" {
#endif

// Hook to allow non-preemptive cooperative scheduling during Python VM execution
void abstractx_repl_vm_hook(void);

#ifdef __cplusplus
}
#endif

// -----------------------------------------------------------------------------
// Options controlling how MicroPython is built
// -----------------------------------------------------------------------------

// Minimum level (CORE_FEATURES overflows SRAM_FAST by ~19 KB) plus what the spec'd scripts
// and board tests use ([SPEC-E906-REPL-06], [SPEC-E906-REPL-15]).
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_MINIMUM)
#define MICROPY_PY_BUILTINS_SLICE   (1)
#define MICROPY_PY_BUILTINS_STR_OP_MODULO (1)
#define MICROPY_FLOAT_IMPL          (MICROPY_FLOAT_IMPL_FLOAT)   // single precision: the E906 has no D
#define MICROPY_LONGINT_IMPL        (MICROPY_LONGINT_IMPL_MPZ)   // 32-bit registers / addresses
#define MICROPY_PY_BUILTINS_BYTEARRAY (1)   // TLP buffers [SPEC-E906-REPL-10]

#define MICROPY_ENABLE_COMPILER     (1)
#define MICROPY_ENABLE_GC           (1)
#define MICROPY_HELPER_REPL         (1)
#define MICROPY_REPL_EVENT_DRIVEN   (1)
// @impl [SPEC-E906-REPL-04] apps/e906_repl_app/SPECIFICATION.md
#define MICROPY_KBD_EXCEPTION       (1)

#define MICROPY_ALLOC_PATH_MAX      (128)
#define MICROPY_ALLOC_PARSE_CHUNK_INIT (16)

// Non-preemptive interleaving hook into the VM dispatch loop
// @impl [SPEC-E906-REPL-04] apps/e906_repl_app/SPECIFICATION.md
#define MICROPY_VM_HOOK_LOOP       abstractx_repl_vm_hook();

// Target machine attributes
#define MICROPY_HW_BOARD_NAME       "Radxa Cubie A5E / A7A"
#define MICROPY_HW_MCU_NAME         "XuanTie E906 (RV32IMAFC)"

typedef long mp_off_t;

#define MP_STATE_PORT MP_STATE_VM

// Native built-in module 'abstractx'
// @impl [SPEC-E906-REPL-06] apps/e906_repl_app/SPECIFICATION.md
extern const struct _mp_obj_module_t mp_module_abstractx;

#define MICROPY_PORT_BUILTIN_MODULES \
    { MP_ROM_QSTR(MP_QSTR_abstractx), MP_ROM_PTR(&mp_module_abstractx) },
