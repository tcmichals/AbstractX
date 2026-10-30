/*
 * modabstractx.c - Native MicroPython module for AbstractX hardware inspection
 *
 * @impl [SPEC-E906-REPL-06] [SPEC-E906-REPL-09] [SPEC-E906-REPL-10] [SPEC-E906-REPL-11] [SPEC-E906-REPL-16] [SPEC-E906-REPL-21] apps/e906_repl_app/SPECIFICATION.md
 */

#include <string.h>

#include "py/runtime.h"
#include "py/obj.h"
#include "py/mphal.h"
#include "mphalport.h"

// Shared staging buffer in SRAM Space 1 (.sram_c) to avoid 256-512 B C stack frames.
static uint8_t s_mod_io_buf[512] __attribute__((section(".sram_c")));

static inline bool is_valid_e906_addr(uintptr_t addr) {
    if ((addr & 0x3) != 0) {
        return false;
    }
    // SRAM ranges:
    // SRAM Space 0 / 1 / B / C: 0x00020000 - 0x00080000
    // Remoteproc shared SRAM: 0x3FFC0000 - 0x40040000
    // Peripheral registers: 0x02000000 - 0x08000000
    if ((addr >= 0x00020000 && addr < 0x00080000) ||
        (addr >= 0x3FFC0000 && addr < 0x40040000) ||
        (addr >= 0x02000000 && addr < 0x08000000)) {
        return true;
    }
    return false;
}

// abstractx.peek(addr) -> uint32 (full 32-bit unsigned value)
// @impl [SPEC-E906-REPL-06] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_peek(mp_obj_t addr_in) {
    uintptr_t addr = (uintptr_t)mp_obj_get_int_truncated(addr_in);
    if (!is_valid_e906_addr(addr)) {
        mp_raise_ValueError(MP_ERROR_TEXT("unaligned or invalid address"));
    }
    volatile uint32_t *reg = (volatile uint32_t *)addr;
    uint32_t val = *reg;
    return mp_obj_new_int_from_uint(val);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_abstractx_peek_obj, mod_abstractx_peek);

// abstractx.peek_hex(addr) -> str ("0x12345678")
// @impl [SPEC-E906-REPL-06] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_peek_hex(mp_obj_t addr_in) {
    uintptr_t addr = (uintptr_t)mp_obj_get_int_truncated(addr_in);
    if (!is_valid_e906_addr(addr)) {
        mp_raise_ValueError(MP_ERROR_TEXT("unaligned or invalid address"));
    }
    volatile uint32_t *reg = (volatile uint32_t *)addr;
    uint32_t val = *reg;
    char hex_str[12];
    const char *hex_digits = "0123456789abcdef";
    hex_str[0] = '0';
    hex_str[1] = 'x';
    for (int i = 7; i >= 0; --i) {
        hex_str[2 + (7 - i)] = hex_digits[(val >> (i * 4)) & 0x0F];
    }
    hex_str[10] = '\0';
    return mp_obj_new_str(hex_str, 10);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_abstractx_peek_hex_obj, mod_abstractx_peek_hex);

// abstractx.poke(addr, val)
// @impl [SPEC-E906-REPL-06] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_poke(mp_obj_t addr_in, mp_obj_t val_in) {
    uintptr_t addr = (uintptr_t)mp_obj_get_int_truncated(addr_in);
    if (!is_valid_e906_addr(addr)) {
        mp_raise_ValueError(MP_ERROR_TEXT("unaligned or invalid address"));
    }
    uint32_t val = (uint32_t)mp_obj_get_int_truncated(val_in);
    volatile uint32_t *reg = (volatile uint32_t *)addr;
    *reg = val;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_abstractx_poke_obj, mod_abstractx_poke);

// Circular DMA / Interrupt-driven UART3 and TWI1 live in e906_periph.cpp ([SPEC-E906-REPL-16] [SPEC-E906-HAL-02]).
extern bool e906_uart3_init(uint32_t baud);
extern size_t e906_uart3_read(uint8_t *dst, size_t max_len);
extern size_t e906_uart3_write(const uint8_t *src, size_t len);
extern uint32_t e906_uart3_lsr(void);
extern bool e906_uart3_set_dma(bool enable);
extern bool e906_uart3_get_dma(void);
extern bool e906_twi1_init(uint32_t freq);
extern int e906_twi1_xfer(uint8_t addr, int reg, const uint8_t *wdata, size_t wlen, uint8_t *rdata, size_t rlen);
extern size_t e906_irq_stats(uint32_t *lines, uint32_t *counts, size_t max);
extern uint32_t e906_irq_spurious(void);
extern uint32_t e906_rpmsg_doorbells(void);
extern uint32_t e906_uart3_rx_dropped(void);
extern uint32_t e906_twi1_timeouts(void);
extern uint32_t e906_mtvec(void);
extern int e906_dma_bench(uint32_t n, uint32_t port, uint32_t *dma_cycles, uint32_t *cpu_cycles);
extern void e906_sleep_ms(uint32_t ms);
extern bool e906_spi_init(uint32_t baud);
extern int e906_spi_xfer(const uint8_t *tx, uint8_t *rx, size_t len, bool dual);

// abstractx.uart_init(baud=9600) -> bool
// @impl [SPEC-E906-REPL-06] [SPEC-E906-REPL-16] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_uart_init(size_t n_args, const mp_obj_t *args) {
    uint32_t baud = 9600;
    if (n_args > 0) {
        baud = (uint32_t)mp_obj_get_int_truncated(args[0]);
    }
    return mp_obj_new_bool(e906_uart3_init(baud));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_abstractx_uart_init_obj, 0, 1, mod_abstractx_uart_init);

// abstractx.uart_read(max_bytes=256) -> bytes (from the RX ring)
// @impl [SPEC-E906-REPL-06] [SPEC-E906-REPL-16] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_uart_read(size_t n_args, const mp_obj_t *args) {
    size_t max_bytes = 256;
    if (n_args > 0) {
        max_bytes = (size_t)mp_obj_get_int_truncated(args[0]);
    }
    if (max_bytes > sizeof(s_mod_io_buf)) {
        max_bytes = sizeof(s_mod_io_buf);
    }
    size_t count = e906_uart3_read(s_mod_io_buf, max_bytes);
    if (count == 0) {
        return mp_const_empty_bytes;
    }
    return mp_obj_new_bytes(s_mod_io_buf, count);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_abstractx_uart_read_obj, 0, 1, mod_abstractx_uart_read);

// abstractx.uart_read_into(buf) -> int (zero-allocation read into writable buffer)
// @impl [SPEC-E906-REPL-06] [SPEC-E906-REPL-16] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_uart_read_into(mp_obj_t buf_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_WRITE);
    size_t count = e906_uart3_read((uint8_t *)bufinfo.buf, bufinfo.len);
    return mp_obj_new_int((mp_int_t)count);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_abstractx_uart_read_into_obj, mod_abstractx_uart_read_into);

// abstractx.uart_dma([enable]) -> bool ([SPEC-E906-HAL-02])
// @impl [SPEC-E906-REPL-06] [SPEC-E906-REPL-16] [SPEC-E906-HAL-02] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_uart_dma(size_t n_args, const mp_obj_t *args) {
    if (n_args > 0) {
        return mp_obj_new_bool(e906_uart3_set_dma(mp_obj_is_true(args[0])));
    }
    return mp_obj_new_bool(e906_uart3_get_dma());
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_abstractx_uart_dma_obj, 0, 1, mod_abstractx_uart_dma);

// abstractx.uart_write(data) -> int (queued; the THR-empty interrupt sends it)
// @impl [SPEC-E906-REPL-06] [SPEC-E906-REPL-16] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_uart_write(mp_obj_t data_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data_in, &bufinfo, MP_BUFFER_READ);
    return mp_obj_new_int((mp_int_t)e906_uart3_write((const uint8_t *)bufinfo.buf, bufinfo.len));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_abstractx_uart_write_obj, mod_abstractx_uart_write);

// abstractx.uart_status() -> int (LSR snapshot)
// @impl [SPEC-E906-REPL-06] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_uart_status(void) {
    return mp_obj_new_int((mp_int_t)e906_uart3_lsr());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_abstractx_uart_status_obj, mod_abstractx_uart_status);

// abstractx.cycles() -> uint64
// @impl [SPEC-E906-REPL-06] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_cycles(void) {
    return mp_obj_new_int_from_ull(mp_hal_ticks_cpu());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_abstractx_cycles_obj, mod_abstractx_cycles);

// abstractx.sleep_ms(ms)
static mp_obj_t mod_abstractx_sleep_ms(mp_obj_t ms_in) {
    mp_uint_t ms = (mp_uint_t)mp_obj_get_int_truncated(ms_in);
    e906_sleep_ms((uint32_t)ms);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_abstractx_sleep_ms_obj, mod_abstractx_sleep_ms);

// abstractx.ticks_ms() -> int
static mp_obj_t mod_abstractx_ticks_ms(void) {
    return mp_obj_new_int_from_uint(mp_hal_ticks_ms());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_abstractx_ticks_ms_obj, mod_abstractx_ticks_ms);

// abstractx.i2c_init(freq=100000) -> bool
// @impl [SPEC-E906-REPL-16] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_i2c_init(size_t n_args, const mp_obj_t *args) {
    uint32_t freq = 100000;
    if (n_args > 0) {
        freq = (uint32_t)mp_obj_get_int_truncated(args[0]);
    }
    return mp_obj_new_bool(e906_twi1_init(freq));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_abstractx_i2c_init_obj, 0, 1, mod_abstractx_i2c_init);

// abstractx.i2c_scan() -> list
static mp_obj_t mod_abstractx_i2c_scan(void) {
    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
        if (e906_twi1_xfer(addr, -1, NULL, 0, NULL, 0) >= 0) {
            mp_obj_list_append(list, mp_obj_new_int(addr));
        }
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_abstractx_i2c_scan_obj, mod_abstractx_i2c_scan);

// abstractx.i2c_read(addr, reg, len) -> bytes (empty on NACK or timeout)
// @impl [SPEC-E906-REPL-06] [SPEC-E906-REPL-16] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_i2c_read(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    uint8_t addr = (uint8_t)mp_obj_get_int_truncated(args[0]);
    int reg = (int)mp_obj_get_int_truncated(args[1]);
    size_t len = (size_t)mp_obj_get_int_truncated(args[2]);
    if (len > 256) len = 256;
    int n = e906_twi1_xfer(addr, reg, NULL, 0, s_mod_io_buf, len);
    if (n <= 0) {
        return mp_const_empty_bytes;
    }
    return mp_obj_new_bytes(s_mod_io_buf, (size_t)n);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_abstractx_i2c_read_obj, 3, 3, mod_abstractx_i2c_read);

// abstractx.i2c_read_into(addr, reg, buf) -> int (zero-allocation read into writable buffer)
// @impl [SPEC-E906-REPL-06] [SPEC-E906-REPL-16] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_i2c_read_into(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    uint8_t addr = (uint8_t)mp_obj_get_int_truncated(args[0]);
    int reg = (int)mp_obj_get_int_truncated(args[1]);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[2], &bufinfo, MP_BUFFER_WRITE);
    size_t len = bufinfo.len > 256 ? 256 : bufinfo.len;
    int n = e906_twi1_xfer(addr, reg, NULL, 0, (uint8_t *)bufinfo.buf, len);
    return mp_obj_new_int(n > 0 ? n : 0);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_abstractx_i2c_read_into_obj, 3, 3, mod_abstractx_i2c_read_into);

// abstractx.i2c_write(addr, reg, data) -> int (bytes written, 0 on NACK or timeout)
static mp_obj_t mod_abstractx_i2c_write(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    uint8_t addr = (uint8_t)mp_obj_get_int_truncated(args[0]);
    int reg = (int)mp_obj_get_int_truncated(args[1]);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[2], &bufinfo, MP_BUFFER_READ);
    int n = e906_twi1_xfer(addr, reg, (const uint8_t *)bufinfo.buf, bufinfo.len, NULL, 0);
    return mp_obj_new_int(n > 0 ? n : 0);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_abstractx_i2c_write_obj, 3, 3, mod_abstractx_i2c_write);

// abstractx.irq_stats() -> dict {line: count, 'mtvec': x, 'spurious': n, 'doorbells': n, ...}
// @impl [SPEC-E906-REPL-16] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_irq_stats(void) {
    uint32_t lines[32];
    uint32_t counts[32];
    size_t n = e906_irq_stats(lines, counts, 32);
    mp_obj_t d = mp_obj_new_dict(n + 5);
    for (size_t i = 0; i < n; ++i) {
        mp_obj_dict_store(d, mp_obj_new_int(lines[i]), mp_obj_new_int_from_uint(counts[i]));
    }
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_mtvec), mp_obj_new_int_from_uint(e906_mtvec()));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_spurious), mp_obj_new_int_from_uint(e906_irq_spurious()));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_doorbells), mp_obj_new_int_from_uint(e906_rpmsg_doorbells()));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_uart_dropped), mp_obj_new_int_from_uint(e906_uart3_rx_dropped()));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_i2c_timeouts), mp_obj_new_int_from_uint(e906_twi1_timeouts()));
    return d;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_abstractx_irq_stats_obj, mod_abstractx_irq_stats);

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

extern size_t abstractx_console_rx_bytes(uint8_t *dest, size_t max_len);
extern bool abstractx_console_has_rx(void);

// abstractx.console_read(max_len) -> bytes
// @impl [SPEC-E906-REPL-06] apps/e906_repl_app/SPECIFICATION.md
// @impl [SPEC-E906-REPL-14] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_console_read(size_t n_args, const mp_obj_t *args) {
    size_t max_len = 64;
    if (n_args > 0) {
        max_len = (size_t)mp_obj_get_int(args[0]);
    }
    if (max_len > 256) max_len = 256;
    size_t n = abstractx_console_rx_bytes(s_mod_io_buf, max_len);
    return mp_obj_new_bytes(s_mod_io_buf, n);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_abstractx_console_read_obj, 0, 1, mod_abstractx_console_read);

// abstractx.console_read_into(buf) -> int (zero-allocation read into writable buffer)
// @impl [SPEC-E906-REPL-06] [SPEC-E906-REPL-14] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_console_read_into(mp_obj_t buf_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_WRITE);
    size_t n = abstractx_console_rx_bytes((uint8_t *)bufinfo.buf, bufinfo.len);
    return mp_obj_new_int((mp_int_t)n);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_abstractx_console_read_into_obj, mod_abstractx_console_read_into);

extern bool abstractx_tlp_send_bytes(const uint8_t *data, size_t len);
extern bool abstractx_tlp_poll_bytes(uint8_t *dst, size_t len);
extern void abstractx_trace_emit(uint32_t task_id, uint8_t state, const char *msg);
extern uint32_t abstractx_tlp_dropped_count(void);

// abstractx.tlp_send(buf) -> bool
// @impl [SPEC-E906-REPL-10] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_tlp_send(mp_obj_t buf_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);
    if (bufinfo.len != 64) {
        mp_raise_ValueError(MP_ERROR_TEXT("TLP packet must be exactly 64 bytes"));
    }
    bool ok = abstractx_tlp_send_bytes((const uint8_t *)bufinfo.buf, 64);
    return ok ? mp_const_true : mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_abstractx_tlp_send_obj, mod_abstractx_tlp_send);

// abstractx.tlp_poll_into(buf) -> bool (zero-allocation pop from ingress TLP ring)
// @impl [SPEC-E906-REPL-10] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_tlp_poll_into(mp_obj_t buf_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_WRITE);
    if (bufinfo.len != 64) {
        mp_raise_ValueError(MP_ERROR_TEXT("TLP packet must be exactly 64 bytes"));
    }
    bool ok = abstractx_tlp_poll_bytes((uint8_t *)bufinfo.buf, 64);
    return ok ? mp_const_true : mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_abstractx_tlp_poll_into_obj, mod_abstractx_tlp_poll_into);

// abstractx.tlp_dropped() -> int
// @impl [SPEC-E906-REPL-10] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_tlp_dropped(void) {
    return mp_obj_new_int_from_uint(abstractx_tlp_dropped_count());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_abstractx_tlp_dropped_obj, mod_abstractx_tlp_dropped);

// abstractx.trace(task_id, state, msg)
// @impl [SPEC-E906-REPL-07] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_trace(size_t n_args, const mp_obj_t *args) {
    uint32_t task_id = (uint32_t)mp_obj_get_int_truncated(args[0]);
    uint8_t state = n_args > 1 ? (uint8_t)mp_obj_get_int_truncated(args[1]) : 1;
    const char *msg = n_args > 2 ? mp_obj_str_get_str(args[2]) : "mpy";
    abstractx_trace_emit(task_id, state, msg);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_abstractx_trace_obj, 1, 3, mod_abstractx_trace);

// abstractx.dma_bench(n, port=1) -> (ok, dma_cycles, cpu_cycles)  [SPEC-E906-HAL-01]
// @impl [SPEC-E906-HAL-01] targets/allwinner_e906/SPECIFICATION.md
static mp_obj_t mod_abstractx_dma_bench(size_t n_args, const mp_obj_t *args) {
    uint32_t dma = 0, cpu = 0;
    uint32_t port = n_args > 1 ? (uint32_t)mp_obj_get_int(args[1]) : 1U;
    int ok = e906_dma_bench((uint32_t)mp_obj_get_int(args[0]), port, &dma, &cpu);
    mp_obj_t t[3] = { mp_obj_new_int(ok), mp_obj_new_int_from_uint(dma), mp_obj_new_int_from_uint(cpu) };
    return mp_obj_new_tuple(3, t);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_abstractx_dma_bench_obj, 1, 2, mod_abstractx_dma_bench);

// abstractx.spi_init(baud=10000000) -> bool
// @impl [SPEC-E906-HAL-03] targets/allwinner_e906/SPECIFICATION.md
// @impl [SPEC-E906-REPL-20] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_spi_init(size_t n_args, const mp_obj_t *args) {
    uint32_t baud = 10000000;
    if (n_args > 0) {
        baud = (uint32_t)mp_obj_get_int_truncated(args[0]);
    }
    return mp_obj_new_bool(e906_spi_init(baud));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_abstractx_spi_init_obj, 0, 1, mod_abstractx_spi_init);

// abstractx.spi_xfer(tx_bytes, dual=False) -> bytes
// @impl [SPEC-E906-HAL-03] targets/allwinner_e906/SPECIFICATION.md
// @impl [SPEC-E906-REPL-20] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_spi_xfer(size_t n_args, const mp_obj_t *args) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_READ);
    bool dual = false;
    if (n_args > 1) {
        dual = mp_obj_is_true(args[1]);
    }
    if (bufinfo.len > sizeof(s_mod_io_buf)) {
        mp_raise_ValueError(MP_ERROR_TEXT("buffer exceeds maximum transfer size"));
    }
    int res = e906_spi_xfer((const uint8_t *)bufinfo.buf, s_mod_io_buf, bufinfo.len, dual);
    if (res < 0) {
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("SPI transfer failed: %d"), res);
    }
    return mp_obj_new_bytes(s_mod_io_buf, (size_t)res);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_abstractx_spi_xfer_obj, 1, 2, mod_abstractx_spi_xfer);

// abstractx.spi_xfer_into(tx_buf, rx_buf, dual=False) -> int
// @impl [SPEC-E906-HAL-03] targets/allwinner_e906/SPECIFICATION.md
// @impl [SPEC-E906-REPL-20] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_spi_xfer_into(size_t n_args, const mp_obj_t *args) {
    mp_buffer_info_t tx_info, rx_info;
    mp_get_buffer_raise(args[0], &tx_info, MP_BUFFER_READ);
    mp_get_buffer_raise(args[1], &rx_info, MP_BUFFER_WRITE);
    bool dual = false;
    if (n_args > 2) {
        dual = mp_obj_is_true(args[2]);
    }
    size_t len = tx_info.len < rx_info.len ? tx_info.len : rx_info.len;
    int res = e906_spi_xfer((const uint8_t *)tx_info.buf, (uint8_t *)rx_info.buf, len, dual);
    if (res < 0) {
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("SPI transfer failed: %d"), res);
    }
    return mp_obj_new_int(res);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_abstractx_spi_xfer_into_obj, 2, 3, mod_abstractx_spi_xfer_into);

// ---------------------------------------------------------------------------
// IMU DRDY auto-DMA ([SPEC-E906-REPL-21]); the channel lives in imu_drdy.cpp
// ---------------------------------------------------------------------------
extern bool e906_imu_drdy_start(void);
extern bool e906_imu_drdy_read_into(uint8_t *buf, uint32_t timeout_ms);
extern void e906_imu_drdy_stop(void);
extern size_t e906_imu_drdy_stats(const char **keys, uint32_t *values, size_t max);

// abstractx.imu_drdy_start() -> bool
// @impl [SPEC-E906-REPL-21] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_imu_drdy_start(void) {
    return mp_obj_new_bool(e906_imu_drdy_start());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_abstractx_imu_drdy_start_obj, mod_abstractx_imu_drdy_start);

// abstractx.imu_drdy_read_into(buf[, timeout_ms]) -> bool; buf >= 22 bytes, no allocation
// @impl [SPEC-E906-REPL-21] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_imu_drdy_read_into(size_t n_args, const mp_obj_t *args) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_WRITE);
    if (bufinfo.len < 22) {
        mp_raise_ValueError(MP_ERROR_TEXT("buffer must be at least 22 bytes"));
    }
    uint32_t timeout_ms = n_args > 1 ? (uint32_t)mp_obj_get_int(args[1]) : 0;
    return e906_imu_drdy_read_into((uint8_t *)bufinfo.buf, timeout_ms) ? mp_const_true : mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_abstractx_imu_drdy_read_into_obj, 1, 2, mod_abstractx_imu_drdy_read_into);

// abstractx.imu_drdy_stop()
// @impl [SPEC-E906-REPL-21] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_imu_drdy_stop(void) {
    e906_imu_drdy_stop();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_abstractx_imu_drdy_stop_obj, mod_abstractx_imu_drdy_stop);

// abstractx.imu_drdy_stats() -> dict with the bench 'A' keys
// @impl [SPEC-E906-REPL-21] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_imu_drdy_stats(void) {
    const char *keys[24];
    uint32_t values[24];
    size_t n = e906_imu_drdy_stats(keys, values, 24);
    mp_obj_t d = mp_obj_new_dict(n);
    for (size_t i = 0; i < n; ++i) {
        mp_obj_dict_store(d, mp_obj_new_str(keys[i], strlen(keys[i])), mp_obj_new_int_from_uint(values[i]));
    }
    return d;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_abstractx_imu_drdy_stats_obj, mod_abstractx_imu_drdy_stats);

static const mp_rom_map_elem_t mp_module_abstractx_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_abstractx) },
    { MP_ROM_QSTR(MP_QSTR_peek), MP_ROM_PTR(&mod_abstractx_peek_obj) },
    { MP_ROM_QSTR(MP_QSTR_peek_hex), MP_ROM_PTR(&mod_abstractx_peek_hex_obj) },
    { MP_ROM_QSTR(MP_QSTR_poke), MP_ROM_PTR(&mod_abstractx_poke_obj) },
    { MP_ROM_QSTR(MP_QSTR_cycles), MP_ROM_PTR(&mod_abstractx_cycles_obj) },
    { MP_ROM_QSTR(MP_QSTR_version), MP_ROM_PTR(&mod_abstractx_version_obj) },
    { MP_ROM_QSTR(MP_QSTR_alive), MP_ROM_PTR(&mod_abstractx_alive_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_init), MP_ROM_PTR(&mod_abstractx_uart_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_read), MP_ROM_PTR(&mod_abstractx_uart_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_read_into), MP_ROM_PTR(&mod_abstractx_uart_read_into_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_dma), MP_ROM_PTR(&mod_abstractx_uart_dma_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_write), MP_ROM_PTR(&mod_abstractx_uart_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_status), MP_ROM_PTR(&mod_abstractx_uart_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_i2c_init), MP_ROM_PTR(&mod_abstractx_i2c_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_i2c_scan), MP_ROM_PTR(&mod_abstractx_i2c_scan_obj) },
    { MP_ROM_QSTR(MP_QSTR_i2c_read), MP_ROM_PTR(&mod_abstractx_i2c_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_i2c_read_into), MP_ROM_PTR(&mod_abstractx_i2c_read_into_obj) },
    { MP_ROM_QSTR(MP_QSTR_i2c_write), MP_ROM_PTR(&mod_abstractx_i2c_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_sleep_ms), MP_ROM_PTR(&mod_abstractx_sleep_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_ms), MP_ROM_PTR(&mod_abstractx_ticks_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_console_read), MP_ROM_PTR(&mod_abstractx_console_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_console_read_into), MP_ROM_PTR(&mod_abstractx_console_read_into_obj) },
    { MP_ROM_QSTR(MP_QSTR_tlp_send), MP_ROM_PTR(&mod_abstractx_tlp_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_tlp_push), MP_ROM_PTR(&mod_abstractx_tlp_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_tlp_poll_into), MP_ROM_PTR(&mod_abstractx_tlp_poll_into_obj) },
    { MP_ROM_QSTR(MP_QSTR_tlp_dropped), MP_ROM_PTR(&mod_abstractx_tlp_dropped_obj) },
    { MP_ROM_QSTR(MP_QSTR_trace), MP_ROM_PTR(&mod_abstractx_trace_obj) },
    { MP_ROM_QSTR(MP_QSTR_irq_stats), MP_ROM_PTR(&mod_abstractx_irq_stats_obj) },
    { MP_ROM_QSTR(MP_QSTR_dma_bench), MP_ROM_PTR(&mod_abstractx_dma_bench_obj) },
    { MP_ROM_QSTR(MP_QSTR_spi_init), MP_ROM_PTR(&mod_abstractx_spi_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_spi_xfer), MP_ROM_PTR(&mod_abstractx_spi_xfer_obj) },
    { MP_ROM_QSTR(MP_QSTR_spi_xfer_into), MP_ROM_PTR(&mod_abstractx_spi_xfer_into_obj) },
    { MP_ROM_QSTR(MP_QSTR_imu_drdy_start), MP_ROM_PTR(&mod_abstractx_imu_drdy_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_imu_drdy_read_into), MP_ROM_PTR(&mod_abstractx_imu_drdy_read_into_obj) },
    { MP_ROM_QSTR(MP_QSTR_imu_drdy_stop), MP_ROM_PTR(&mod_abstractx_imu_drdy_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_imu_drdy_stats), MP_ROM_PTR(&mod_abstractx_imu_drdy_stats_obj) },
};

static MP_DEFINE_CONST_DICT(mp_module_abstractx_globals, mp_module_abstractx_globals_table);

const mp_obj_module_t mp_module_abstractx = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_abstractx_globals,
};

MP_REGISTER_MODULE(MP_QSTR_abstractx, mp_module_abstractx);


