/*
 * modabstractx.c - Native MicroPython module for AbstractX hardware inspection
 *
 * @impl [SPEC-E906-REPL-06] [SPEC-E906-REPL-09] [SPEC-E906-REPL-10] apps/e906_repl_app/SPECIFICATION.md
 */

#include "py/runtime.h"
#include "py/obj.h"
#include "py/mphal.h"
#include "mphalport.h"

// abstractx.peek(addr) -> uint32 (truncated to 30-bit signed if high bit set)
// @impl [SPEC-E906-REPL-06] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_peek(mp_obj_t addr_in) {
    uintptr_t addr = (uintptr_t)mp_obj_get_int_truncated(addr_in);
    volatile uint32_t *reg = (volatile uint32_t *)addr;
    uint32_t val = *reg;
    return mp_obj_new_int((mp_int_t)(val & 0x3FFFFFFFU));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_abstractx_peek_obj, mod_abstractx_peek);

// abstractx.peek_hex(addr) -> str ("0x12345678")
// @impl [SPEC-E906-REPL-06] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_peek_hex(mp_obj_t addr_in) {
    uintptr_t addr = (uintptr_t)mp_obj_get_int_truncated(addr_in);
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
    uint32_t val = (uint32_t)mp_obj_get_int_truncated(val_in);
    volatile uint32_t *reg = (volatile uint32_t *)addr;
    *reg = val;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_abstractx_poke_obj, mod_abstractx_poke);

// Hardware Register Definitions for UART3, CCU, and PIO
#define UART3_BASE_ADDR         0x02500C00U
#define CCU_BASE_ADDR           0x02001000U
#define PIO_BASE_ADDR           0x02000000U

#define CCU_UART_BGR            (*(volatile uint32_t *)(CCU_BASE_ADDR + 0x090CU))
#define PIO_PI_CFG1             (*(volatile uint32_t *)(PIO_BASE_ADDR + 0x0184U))
#define PIO_PI_PULL0            (*(volatile uint32_t *)(PIO_BASE_ADDR + 0x01A4U))

#define HW_UART3_RBR            (*(volatile uint32_t *)(UART3_BASE_ADDR + 0x00U))
#define HW_UART3_THR            (*(volatile uint32_t *)(UART3_BASE_ADDR + 0x00U))
#define HW_UART3_DLL            (*(volatile uint32_t *)(UART3_BASE_ADDR + 0x00U))
#define HW_UART3_DLH            (*(volatile uint32_t *)(UART3_BASE_ADDR + 0x04U))
#define HW_UART3_IER            (*(volatile uint32_t *)(UART3_BASE_ADDR + 0x04U))
#define HW_UART3_FCR            (*(volatile uint32_t *)(UART3_BASE_ADDR + 0x08U))
#define HW_UART3_LCR            (*(volatile uint32_t *)(UART3_BASE_ADDR + 0x0CU))
#define HW_UART3_MCR            (*(volatile uint32_t *)(UART3_BASE_ADDR + 0x10U))
#define HW_UART3_LSR            (*(volatile uint32_t *)(UART3_BASE_ADDR + 0x14U))

// abstractx.uart_init(baud=9600) -> bool
// @impl [SPEC-E906-REPL-06] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_uart_init(size_t n_args, const mp_obj_t *args) {
    uint32_t baud = 9600;
    if (n_args > 0) {
        baud = (uint32_t)mp_obj_get_int_truncated(args[0]);
    }

    // 1. Deassert reset (bit 19) and enable clock gating (bit 3) for UART3
    CCU_UART_BGR |= (1U << 19) | (1U << 3);

    // 2. Configure PIO Port I: PI11 (TX) and PI12 (RX) to UART3 (Mux 2: 0b0010)
    // PI11 is bits [15:12], PI12 is bits [19:16] in PIO_PI_CFG1
    PIO_PI_CFG1 = (PIO_PI_CFG1 & ~(0xFFU << 12)) | (0x22U << 12);

    // 3. Configure Pull-Up on PI12 (RX)
    PIO_PI_PULL0 = (PIO_PI_PULL0 & ~(0x03U << 24)) | (1U << 24);

    // 4. Configure DesignWare 8250 UART3
    HW_UART3_IER = 0x00; // Disable interrupts during config
    HW_UART3_FCR = 0x87; // Enable FIFO, reset TX & RX FIFO

    uint32_t apb_clock = 24000000U;
    uint32_t divisor = (apb_clock + (8 * baud)) / (16 * baud);
    if (divisor == 0) divisor = 1;

    HW_UART3_LCR = 0x83; // 8N1 + DLAB enable
    HW_UART3_DLL = divisor & 0xFFU;
    HW_UART3_DLH = (divisor >> 8) & 0xFFU;
    HW_UART3_LCR = 0x03; // Clear DLAB (8 bits, no parity, 1 stop bit)
    HW_UART3_MCR = 0x03; // RTS/DTR high

    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_abstractx_uart_init_obj, 0, 1, mod_abstractx_uart_init);

// abstractx.uart_read(max_bytes=128) -> bytes
// @impl [SPEC-E906-REPL-06] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_uart_read(size_t n_args, const mp_obj_t *args) {
    size_t max_bytes = 256;
    if (n_args > 0) {
        max_bytes = (size_t)mp_obj_get_int_truncated(args[0]);
    }
    if (max_bytes > 512) {
        max_bytes = 512;
    }

    uint8_t rx_buf[512];
    size_t count = 0;

    // Drain available bytes from hardware FIFO (Bit 0 of LSR is Data Ready)
    while ((HW_UART3_LSR & 0x01U) && (count < max_bytes)) {
        rx_buf[count++] = (uint8_t)(HW_UART3_RBR & 0xFFU);
    }

    if (count == 0) {
        return mp_const_empty_bytes;
    }
    return mp_obj_new_bytes(rx_buf, count);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_abstractx_uart_read_obj, 0, 1, mod_abstractx_uart_read);

// abstractx.uart_write(data) -> int
// @impl [SPEC-E906-REPL-06] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_uart_write(mp_obj_t data_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data_in, &bufinfo, MP_BUFFER_READ);

    const uint8_t *src = (const uint8_t *)bufinfo.buf;
    for (size_t i = 0; i < bufinfo.len; ++i) {
        // Wait until Transmit Holding Register Empty (THRE / bit 5) is set
        while (!(HW_UART3_LSR & (1U << 5))) {
            __asm__ volatile("" : : : "memory");
        }
        HW_UART3_THR = src[i];
    }
    return mp_obj_new_int(bufinfo.len);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_abstractx_uart_write_obj, mod_abstractx_uart_write);

// abstractx.uart_status() -> int (returns LSR register)
// @impl [SPEC-E906-REPL-06] apps/e906_repl_app/SPECIFICATION.md
static mp_obj_t mod_abstractx_uart_status(void) {
    uint32_t lsr = HW_UART3_LSR;
    return mp_obj_new_int((mp_int_t)(lsr & 0xFFU));
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
    mp_hal_delay_ms(ms);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_abstractx_sleep_ms_obj, mod_abstractx_sleep_ms);

// abstractx.ticks_ms() -> int
static mp_obj_t mod_abstractx_ticks_ms(void) {
    return mp_obj_new_int_from_uint(mp_hal_ticks_ms());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_abstractx_ticks_ms_obj, mod_abstractx_ticks_ms);

// Hardware Register Definitions for TWI1 (I2C1 on Pins 3 & 5: PB5/PB4)
#define TWI1_BASE_ADDR          0x02502400U
#define CCU_TWI_BGR             (*(volatile uint32_t *)(CCU_BASE_ADDR + 0x091CU))
#define PIO_PB_CFG0             (*(volatile uint32_t *)(PIO_BASE_ADDR + 0x0030U))

#define HW_TWI1_ADDR            (*(volatile uint32_t *)(TWI1_BASE_ADDR + 0x00U))
#define HW_TWI1_XADDR           (*(volatile uint32_t *)(TWI1_BASE_ADDR + 0x04U))
#define HW_TWI1_DATA            (*(volatile uint32_t *)(TWI1_BASE_ADDR + 0x08U))
#define HW_TWI1_CNTR            (*(volatile uint32_t *)(TWI1_BASE_ADDR + 0x0CU))
#define HW_TWI1_STAT            (*(volatile uint32_t *)(TWI1_BASE_ADDR + 0x10U))
#define HW_TWI1_CCR             (*(volatile uint32_t *)(TWI1_BASE_ADDR + 0x14U))
#define HW_TWI1_SRST            (*(volatile uint32_t *)(TWI1_BASE_ADDR + 0x18U))

#define TWI_CNTR_BUSEN          (1U << 6)
#define TWI_CNTR_M_STA          (1U << 5)
#define TWI_CNTR_M_STP          (1U << 4)
#define TWI_CNTR_INT_FLG        (1U << 3)
#define TWI_CNTR_A_ACK          (1U << 2)

static bool twi1_wait_flag(void) {
    uint32_t timeout = 50000;
    while (!(HW_TWI1_CNTR & TWI_CNTR_INT_FLG)) {
        if (--timeout == 0) return false;
        abstractx_repl_vm_hook();
    }
    return true;
}

static bool twi1_start(void) {
    HW_TWI1_CNTR = TWI_CNTR_BUSEN | TWI_CNTR_M_STA | TWI_CNTR_INT_FLG;
    if (!twi1_wait_flag()) return false;
    uint32_t stat = HW_TWI1_STAT & 0xFFU;
    return (stat == 0x08U || stat == 0x10U);
}

static void twi1_stop(void) {
    HW_TWI1_CNTR = TWI_CNTR_BUSEN | TWI_CNTR_M_STP | TWI_CNTR_INT_FLG;
    uint32_t timeout = 5000;
    while ((HW_TWI1_CNTR & TWI_CNTR_M_STP) && --timeout) {
        abstractx_repl_vm_hook();
    }
}

static bool twi1_send_byte(uint8_t byte, uint32_t expected_stat) {
    HW_TWI1_DATA = byte;
    HW_TWI1_CNTR = TWI_CNTR_BUSEN | TWI_CNTR_INT_FLG;
    if (!twi1_wait_flag()) return false;
    return (HW_TWI1_STAT & 0xFFU) == expected_stat;
}

static uint8_t twi1_read_byte(bool ack) {
    uint32_t ctrl = TWI_CNTR_BUSEN | TWI_CNTR_INT_FLG;
    if (ack) {
        ctrl |= TWI_CNTR_A_ACK;
    }
    HW_TWI1_CNTR = ctrl;
    if (!twi1_wait_flag()) return 0xFF;
    return (uint8_t)(HW_TWI1_DATA & 0xFFU);
}

// abstractx.i2c_init(freq=100000) -> bool
static mp_obj_t mod_abstractx_i2c_init(size_t n_args, const mp_obj_t *args) {
    uint32_t freq = 100000;
    if (n_args > 0) {
        freq = (uint32_t)mp_obj_get_int_truncated(args[0]);
    }

    // 1. Deassert reset (bit 17) and enable clock (bit 1) for TWI1
    CCU_TWI_BGR |= (1U << 17) | (1U << 1);

    // 2. Configure PB4 & PB5 to TWI1 (Mux 2: 0x22 << 16)
    PIO_PB_CFG0 = (PIO_PB_CFG0 & ~(0xFFU << 16)) | (0x22U << 16);

    // 3. Reset TWI1 controller
    HW_TWI1_SRST = 1;
    for (volatile int i = 0; i < 500; ++i);

    // 4. Set accurate clock divisor on 24 MHz APB clock:
    // F0 = Fin / (2^N * (M + 1) * 10)
    // 400 kHz: Fin/60 => N=1 (2^1=2), M=2 => 2 * 3 * 10 = 60 => CCR = 0x12
    // 100 kHz: Fin/240 => N=2 (2^2=4), M=5 => 4 * 6 * 10 = 240 => CCR = 0x25
    if (freq >= 400000) {
        HW_TWI1_CCR = 0x12U; // 400 kHz Fast Mode
    } else {
        HW_TWI1_CCR = 0x25U; // 100 kHz Standard Mode
    }

    // 5. Enable TWI bus
    HW_TWI1_CNTR = TWI_CNTR_BUSEN;
    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_abstractx_i2c_init_obj, 0, 1, mod_abstractx_i2c_init);

// abstractx.i2c_scan() -> list
static mp_obj_t mod_abstractx_i2c_scan(void) {
    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
        if (twi1_start()) {
            if (twi1_send_byte((addr << 1) | 0, 0x18U)) {
                mp_obj_list_append(list, mp_obj_new_int(addr));
            }
            twi1_stop();
        }
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_abstractx_i2c_scan_obj, mod_abstractx_i2c_scan);

// abstractx.i2c_read(addr, reg, len) -> bytes
static mp_obj_t mod_abstractx_i2c_read(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    uint8_t addr = (uint8_t)mp_obj_get_int_truncated(args[0]);
    uint8_t reg = (uint8_t)mp_obj_get_int_truncated(args[1]);
    size_t len = (size_t)mp_obj_get_int_truncated(args[2]);
    if (len > 256) len = 256;

    if (!twi1_start()) { twi1_stop(); return mp_const_empty_bytes; }
    if (!twi1_send_byte((addr << 1) | 0, 0x18U)) { twi1_stop(); return mp_const_empty_bytes; }
    if (!twi1_send_byte(reg, 0x28U)) { twi1_stop(); return mp_const_empty_bytes; }
    if (!twi1_start()) { twi1_stop(); return mp_const_empty_bytes; }
    if (!twi1_send_byte((addr << 1) | 1, 0x40U)) { twi1_stop(); return mp_const_empty_bytes; }

    uint8_t buf[256];
    for (size_t i = 0; i < len; ++i) {
        bool ack = (i + 1 < len);
        buf[i] = twi1_read_byte(ack);
    }
    twi1_stop();
    return mp_obj_new_bytes(buf, len);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_abstractx_i2c_read_obj, 3, 3, mod_abstractx_i2c_read);

// abstractx.i2c_write(addr, reg, data) -> int
static mp_obj_t mod_abstractx_i2c_write(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    uint8_t addr = (uint8_t)mp_obj_get_int_truncated(args[0]);
    uint8_t reg = (uint8_t)mp_obj_get_int_truncated(args[1]);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[2], &bufinfo, MP_BUFFER_READ);

    if (!twi1_start()) { twi1_stop(); return mp_obj_new_int(0); }
    if (!twi1_send_byte((addr << 1) | 0, 0x18U)) { twi1_stop(); return mp_obj_new_int(0); }
    if (!twi1_send_byte(reg, 0x28U)) { twi1_stop(); return mp_obj_new_int(0); }

    const uint8_t *src = (const uint8_t *)bufinfo.buf;
    size_t written = 0;
    for (size_t i = 0; i < bufinfo.len; ++i) {
        if (!twi1_send_byte(src[i], 0x28U)) break;
        written++;
    }
    twi1_stop();
    return mp_obj_new_int(written);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_abstractx_i2c_write_obj, 3, 3, mod_abstractx_i2c_write);

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
    { MP_ROM_QSTR(MP_QSTR_peek_hex), MP_ROM_PTR(&mod_abstractx_peek_hex_obj) },
    { MP_ROM_QSTR(MP_QSTR_poke), MP_ROM_PTR(&mod_abstractx_poke_obj) },
    { MP_ROM_QSTR(MP_QSTR_cycles), MP_ROM_PTR(&mod_abstractx_cycles_obj) },
    { MP_ROM_QSTR(MP_QSTR_version), MP_ROM_PTR(&mod_abstractx_version_obj) },
    { MP_ROM_QSTR(MP_QSTR_alive), MP_ROM_PTR(&mod_abstractx_alive_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_init), MP_ROM_PTR(&mod_abstractx_uart_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_read), MP_ROM_PTR(&mod_abstractx_uart_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_write), MP_ROM_PTR(&mod_abstractx_uart_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_status), MP_ROM_PTR(&mod_abstractx_uart_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_i2c_init), MP_ROM_PTR(&mod_abstractx_i2c_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_i2c_scan), MP_ROM_PTR(&mod_abstractx_i2c_scan_obj) },
    { MP_ROM_QSTR(MP_QSTR_i2c_read), MP_ROM_PTR(&mod_abstractx_i2c_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_i2c_write), MP_ROM_PTR(&mod_abstractx_i2c_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_sleep_ms), MP_ROM_PTR(&mod_abstractx_sleep_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_ms), MP_ROM_PTR(&mod_abstractx_ticks_ms_obj) },
};

static MP_DEFINE_CONST_DICT(mp_module_abstractx_globals, mp_module_abstractx_globals_table);

const mp_obj_module_t mp_module_abstractx = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_abstractx_globals,
};

MP_REGISTER_MODULE(MP_QSTR_abstractx, mp_module_abstractx);

