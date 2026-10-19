/*
 * modabstractx.c - Native MicroPython module for AbstractX hardware inspection
 *
 * @impl [SPEC-E906-REPL-06] apps/e906_repl_app/SPECIFICATION.md
 */

#include "py/runtime.h"
#include "py/obj.h"
#include "py/mphal.h"
#include "mphalport.h"

// abstractx.peek(addr) -> uint32
// @impl [SPEC-E906-REPL-06] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_peek(mp_obj_t addr_in) {
    uintptr_t addr = (uintptr_t)mp_obj_get_int_truncated(addr_in);
    volatile uint32_t *reg = (volatile uint32_t *)addr;
    uint32_t val = *reg;
    return mp_obj_new_int_from_uint(val);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_abstractx_peek_obj, mod_abstractx_peek);

// abstractx.poke(addr, val)
// @impl [SPEC-E906-REPL-06] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_poke(mp_obj_t addr_in, mp_obj_t val_in) {
    uintptr_t addr = (uintptr_t)mp_obj_get_int_truncated(addr_in);
    uint32_t val = (uint32_t)mp_obj_get_int_truncated(val_in);
    volatile uint32_t *reg = (volatile uint32_t *)addr;
    *reg = val;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_abstractx_poke_obj, mod_abstractx_poke);

// abstractx.cycles() -> uint64
// @impl [SPEC-E906-REPL-06] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_cycles(void) {
    return mp_obj_new_int_from_ull(mp_hal_ticks_cpu());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_abstractx_cycles_obj, mod_abstractx_cycles);

// abstractx.version() -> str
// @impl [SPEC-E906-REPL-06] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_version(void) {
    const char *ver = "AbstractX 1.0.0 (E906 MicroPython REPL)";
    return mp_obj_new_str(ver, strlen(ver));
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_abstractx_version_obj, mod_abstractx_version);

// abstractx.alive() -> bool
// @impl [SPEC-E906-REPL-06] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_alive(void) {
    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_abstractx_alive_obj, mod_abstractx_alive);

static const mp_rom_map_elem_t mp_module_abstractx_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_abstractx) },
    { MP_ROM_QSTR(MP_QSTR_peek), MP_ROM_PTR(&mod_abstractx_peek_obj) },
    { MP_ROM_QSTR(MP_QSTR_poke), MP_ROM_PTR(&mod_abstractx_poke_obj) },
    { MP_ROM_QSTR(MP_QSTR_cycles), MP_ROM_PTR(&mod_abstractx_cycles_obj) },
    { MP_ROM_QSTR(MP_QSTR_version), MP_ROM_PTR(&mod_abstractx_version_obj) },
    { MP_ROM_QSTR(MP_QSTR_alive), MP_ROM_PTR(&mod_abstractx_alive_obj) },
};

static MP_DEFINE_CONST_DICT(mp_module_abstractx_globals, mp_module_abstractx_globals_table);

const mp_obj_module_t mp_module_abstractx = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_abstractx_globals,
};

MP_REGISTER_MODULE(MP_QSTR_abstractx, mp_module_abstractx);
