#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
E906 interrupt paths on the board ([SPEC-E906-IRQ-01..04], [SPEC-E906-REPL-16]).
Each test reads abstractx.irq_stats() before and after exercising one source and
requires that source's CLIC line count to grow. On firmware without the CLIC path
every count stays 0 and these tests fail.
"""
# @verifies [SPEC-E906-IRQ-01] [SPEC-E906-IRQ-02] [SPEC-E906-IRQ-03] [SPEC-E906-IRQ-04] [SPEC-E906-CORO-02] targets/allwinner_e906/SPECIFICATION.md
# @verifies [SPEC-E906-REPL-16] apps/e906_repl_app/SPECIFICATION.md

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from board_common import skip  # noqa: E402
from e906_link import on_e906_board, repl_exec  # noqa: E402

LINE_TIMER, LINE_MSGBOX, LINE_UART3, LINE_TWI1, LINE_DMA = 7, 17, 90, 91, 88
COMPASS_ADDRS = (0x0D, 0x1E)


def _require_board():
    if not on_e906_board():
        skip("not an E906 board (no remoteproc0)")


def _run(code: str, timeout_s: float = 10.0) -> str:
    out, err = repl_exec(code, timeout_s)
    assert not err.strip(), f"E906 raised: {err.strip()[-300:]}"
    return out


def _stats() -> dict:
    out = _run("import abstractx\nprint('IRQ', abstractx.irq_stats())")
    line = [ln for ln in out.splitlines() if ln.startswith("IRQ ")]
    assert line, f"irq_stats() missing (old firmware?): {out.strip()[-200:]}"
    raw = eval(line[-1][4:], {"__builtins__": {}})  # a dict literal of ints from the E906
    return {k: v for k, v in raw.items()}


def test_mtvec_is_clic_mode():
    _require_board()
    st = _stats()
    assert st["mtvec"] & 3 == 3, f"mtvec 0x{st['mtvec']:08x} is not CLIC mode (MODE=3)"
    assert st["mtvec"] & 0x3C == 0, f"mtvec base 0x{st['mtvec'] & ~3:08x} not 64-byte aligned"


def test_rpmsg_wakes_on_message_box_interrupt():
    _require_board()
    before = _stats()
    for _ in range(5):
        _run("print(1)")
    after = _stats()
    grew = after.get(LINE_MSGBOX, 0) - before.get(LINE_MSGBOX, 0)
    assert grew >= 5, f"CLIC line 17 grew by {grew} over 5 REPL round trips: {before} -> {after}"
    assert after["doorbells"] > before["doorbells"]


def test_raw_repl_reply_needs_no_extra_input():
    _require_board()
    t0 = time.monotonic()
    out = _run("print('tail')", timeout_s=3.0)  # TimeoutError if the '\x04\x04>' tail is held back
    assert "tail" in out
    assert time.monotonic() - t0 < 3.0


def test_timer_interrupt_ends_sleep():
    _require_board()
    before = _stats()
    t0 = time.monotonic()
    _run("import abstractx\nabstractx.sleep_ms(300)")
    dt = time.monotonic() - t0
    after = _stats()
    assert after.get(LINE_TIMER, 0) > before.get(LINE_TIMER, 0), f"line 7 never fired: {before} -> {after}"
    assert 0.3 <= dt < 2.0, f"sleep_ms(300) took {dt:.2f} s"


def test_uart3_rx_by_interrupt():
    _require_board()
    before = _stats()
    # drops are counted from after uart_init(): an earlier run may have left the ring full
    out = _run("import abstractx\nabstractx.uart_init(9600)\nd0 = abstractx.irq_stats()['uart_dropped']\n"
               "abstractx.sleep_ms(1000)\nd = abstractx.uart_read(512)\n"
               "print('GPS', len(d), abstractx.irq_stats()['uart_dropped'] - d0)", timeout_s=8)
    after = _stats()
    fields = [ln for ln in out.splitlines() if ln.startswith("GPS ")][-1].split()
    n, dropped = int(fields[1]), int(fields[2])
    grew = after.get(LINE_UART3, 0) - before.get(LINE_UART3, 0)
    assert n > 0, f"no GPS bytes in the RX ring after 1 s: {out.strip()[-200:]}"
    assert grew > 0, f"UART3 bytes arrived but CLIC line 90 never fired ({before} -> {after})"
    assert dropped == 0, f"RX ring overflowed by {dropped} bytes in 1 s"


def test_twi1_compass_by_interrupt():
    _require_board()
    before = _stats()
    out = _run("import abstractx\nabstractx.i2c_init(400000)\nprint('SCAN', abstractx.i2c_scan())")
    after = _stats()
    found = [a for a in COMPASS_ADDRS if f"{a}" in out.split("SCAN", 1)[-1]]
    assert found, f"no compass at {[hex(a) for a in COMPASS_ADDRS]}: {out.strip()[-200:]}"
    grew = after.get(LINE_TWI1, 0) - before.get(LINE_TWI1, 0)
    assert grew >= 112, f"i2c_scan() of 112 addresses raised only {grew} TWI1 interrupts"
    assert after["i2c_timeouts"] == before["i2c_timeouts"], "an I2C transfer timed out"


def test_no_spurious_interrupts():
    _require_board()
    st = _stats()
    assert st["spurious"] == 0, f"{st['spurious']} interrupts on lines nobody owns: {st}"


def test_dma_copy_completes_by_interrupt():
    """[SPEC-E906-HAL-01]: an E906 DMA channel copies SRAM Space 0 -> Space 1 by physical address,
    completes on CLIC line 88, and the controller routes channels 8..15 to the E906 only."""
    # @verifies [SPEC-E906-HAL-01] targets/allwinner_e906/SPECIFICATION.md
    _require_board()
    before = _stats().get(LINE_DMA, 0)
    out = _run("import abstractx\n"
               "for port in (1, 0):\n"
               "    for n in (4, 64, 1024):\n"
               "        print('BENCH', port, n, abstractx.dma_bench(n, port))\n"
               "print('EN', abstractx.peek_hex(0x03002034), abstractx.peek_hex(0x03002038))", 20.0)
    rows = [ln.split(" ", 3) for ln in out.splitlines() if ln.startswith("BENCH ")]
    assert len(rows) == 6, f"dma_bench missing (old firmware?): {out.strip()[-200:]}"
    for _, port, n, res in rows:
        ok, dma, cpu = eval(res, {"__builtins__": {}})
        assert ok == 1, f"port {port}, {n} B: dma_bench returned {ok} (1 = data matches, -1 = no interrupt)"
        assert 0 < dma < 200_000, f"port {port}, {n} B: {dma} cycles"
    assert "EN 0x000000ff 0x0000ff00" in out, f"DMAC IRQ routing not set: {out.strip()[-120:]}"
    assert _stats().get(LINE_DMA, 0) - before >= 6, "line 88 did not fire once per transfer"
