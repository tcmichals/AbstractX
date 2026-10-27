/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * i2c.cpp - MicroPython glue for TWI1 (abstractx.i2c_*). The driver is the target's E906I2c.
 *
 * @impl [SPEC-E906-REPL-16] apps/e906_repl_app/SPECIFICATION.md
 * @impl [SPEC-E906-HAL-04] targets/allwinner_e906/SPECIFICATION.md
 */

#include "hal/i2c.hpp"
#include "repl_wait.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace {

using abstractx::hal::E906I2c;
using abstractx::hal::I2cStatus;

// Static, not on the caller's stack: a Ctrl-C can unwind the caller mid-transfer.
// In .sram_c (SRAM Space 1) to save SRAM_FAST.
__attribute__((section(".sram_c"))) uint8_t s_wbuf[1 + 256];  // register byte + data
__attribute__((section(".sram_c"))) uint8_t s_rbuf[256];

E906I2c& twi() noexcept { return abstractx::hal::get_e906_twi1(); }

} // namespace

extern "C" {

bool e906_twi1_init(uint32_t freq) {
    E906I2c::set_wait_hook(repl_wait_step);
    abstractx::hal::I2cConfig cfg{};
    cfg.frequency_hz = freq;
    return twi().init(cfg);
}

// One transaction: START, addr+W, [reg], [write bytes], [repeated START, addr+R, read bytes], STOP.
// reg < 0: no register byte. Returns bytes read (or written), -1 on NACK/bus error, -2 on timeout.
int e906_twi1_xfer(uint8_t addr, int reg, const uint8_t* wdata, size_t wlen, uint8_t* rdata, size_t rlen) {
    if (wlen > sizeof(s_wbuf) - 1 || rlen > sizeof(s_rbuf)) return -1;
    size_t n = 0;
    if (reg >= 0) s_wbuf[n++] = static_cast<uint8_t>(reg);
    for (size_t i = 0; i < wlen; ++i) s_wbuf[n++] = wdata[i];

    const I2cStatus st = twi().transfer_sync(addr, std::span<const uint8_t>(s_wbuf, n),
                                             std::span<uint8_t>(s_rbuf, rlen));
    if (st == I2cStatus::Timeout) return -2;
    if (st != I2cStatus::Ok) return -1;
    const size_t got = twi().bytes_read();
    for (size_t i = 0; i < got && rdata; ++i) rdata[i] = s_rbuf[i];
    return static_cast<int>(rlen > 0 ? got : wlen);
}

uint32_t e906_twi1_timeouts(void) { return twi().timeouts(); }

} // extern "C"
