/*
 * mphalport.h - MicroPython HAL declarations for E906
 *
 * @impl [SPEC-E906-REPL-03] apps/e906_repl_app/SPECIFICATION.md
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "shared/runtime/interrupt_char.h"

// The port moves the cursor itself: MicroPython's VT100 helper calls snprintf, which links newlib's
// _realloc_r ([SPEC-E906-MEM-03]).
// @impl [SPEC-E906-MEM-03] targets/allwinner_e906/SPECIFICATION.md
#define MICROPY_HAL_HAS_VT100 (0)
#ifdef __cplusplus
extern "C" {
#endif
void mp_hal_move_cursor_back(unsigned int pos);
void mp_hal_erase_line_from_cursor(unsigned int n_chars_to_erase);
#ifdef __cplusplus
}
#endif
