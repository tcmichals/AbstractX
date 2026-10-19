/*
 * mphalport.h - MicroPython HAL declarations for E906
 *
 * @impl [SPEC-E906-REPL-03] apps/e906_repl_app/SPECIFICATION.md
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void mp_hal_set_interrupt_char(char c) {
    (void)c;
}

#ifdef __cplusplus
}
#endif
