# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""RP2040 ISA conformance of asp_pio_tile, running programs assembled by the Raspberry Pi
pioasm (rtl/pio/programs/conformance/conformance.pio). One test per [SPEC-PIO-02] rule.

@verifies [SPEC-PIO-02] rtl/pio/SPECIFICATION.md
@verifies [SPEC-PIO-11] rtl/pio/SPECIFICATION.md
"""

import os
import sys
from pathlib import Path

import cocotb
from cocotb.triggers import ClockCycles

from test_asp_pio_cocotb import (init_tile, wb_write, wb_read, REG_CTRL, REG_IRQ, IRAM_BASE,
                                 SM_CLKDIV, SM_EXECCTRL, SM_SHIFTCTRL, SM_PINCTRL, SM_TXF,
                                 SM_RXF, SM_STAT)

sys.path.insert(0, str(Path(os.environ.get("ABSTRACTX_ROOT", Path(__file__).resolve().parents[2])) / "tools"))
from pio_programs import load_program  # noqa: E402

REG_FIFO_STAT = 0x08
SM_INSTR = lambda n: 0x100 + 0x20 * n + 0x18  # noqa: E731

SET_PIN0 = 1 << 26                  # PINCTRL: SET_COUNT=1, SET_BASE=0
AUTOPUSH, AUTOPULL = 1 << 16, 1 << 17
OUT_RIGHT = 1 << 19


def push_thresh(n): return (n & 31) << 20
def pull_thresh(n): return (n & 31) << 25


async def run(dut, name, sm=0, pinctrl=0, shiftctrl=0, execctrl=0, enable=True):
    """Load `name` at offset 0 and configure SM `sm` from the image (wrap, side-set)."""
    p = load_program(name)
    for i, w in enumerate(p["instructions"]):
        await wb_write(dut, IRAM_BASE + 4 * i, w)
    ss = p["sideset"]
    if ss["size"]:
        pinctrl |= ss["size"] << 29
        execctrl |= (1 << 30) if ss["optional"] else 0
        execctrl |= (1 << 29) if ss["pindirs"] else 0
    execctrl |= (p["wrap"] << 12) | (p["wrap_target"] << 7)
    await wb_write(dut, SM_CLKDIV(sm), 1 << 16)
    await wb_write(dut, SM_EXECCTRL(sm), execctrl)
    await wb_write(dut, SM_SHIFTCTRL(sm), shiftctrl)
    await wb_write(dut, SM_PINCTRL(sm), pinctrl)
    await wb_write(dut, REG_CTRL, (1 << (sm + 4)))           # restart (disabled)
    await wb_write(dut, SM_INSTR(sm), 0x0000)                # jmp 0, as pio_sm_exec does
    if enable:
        await wb_write(dut, REG_CTRL, (1 << sm))
    return p


async def rx_empty(dut, sm):
    return (await wb_read(dut, REG_FIFO_STAT) >> (28 + sm)) & 1


async def drain(dut, sm, limit=8):
    out = []
    while len(out) < limit and not await rx_empty(dut, sm):
        out.append(await wb_read(dut, SM_RXF(sm)))
    return out


def pin(dut, n):
    return (int(dut.o_gpio_out.value) >> n) & 1


@cocotb.test()
async def test_pull_block_stalls_on_empty_fifo(dut):
    """Rule 1: `pull block` on an empty TX FIFO stalls; the PC does not advance."""
    await init_tile(dut)
    await run(dut, "c_pull_block", pinctrl=SET_PIN0)
    await ClockCycles(dut.clk, 50)
    assert pin(dut, 0) == 0, "set pins,1 ran although the FIFO was empty"
    assert await wb_read(dut, SM_STAT(0)) & 0x1F == 0, "PC advanced past the blocking pull"
    await wb_write(dut, SM_TXF(0), 0x12345678)
    await ClockCycles(dut.clk, 20)
    assert await drain(dut, 0) == [0x12345678]


@cocotb.test()
async def test_pull_noblock_copies_x(dut):
    """Rule 2: `pull noblock` on an empty FIFO copies X into OSR."""
    await init_tile(dut)
    await run(dut, "c_pull_noblock")
    await ClockCycles(dut.clk, 30)
    assert await drain(dut, 0) == [21]


@cocotb.test()
async def test_push_noblock_full_is_dropped(dut):
    """Rule 2: `push noblock` on a full RX FIFO is dropped and does not stall."""
    await init_tile(dut)
    await run(dut, "c_push_noblock", pinctrl=SET_PIN0)
    await ClockCycles(dut.clk, 60)
    assert pin(dut, 0) == 1, "push noblock stalled on a full FIFO"
    assert await drain(dut, 0) == [4, 3, 2, 1], "the first four pushes must be kept, the fifth dropped"


@cocotb.test()
async def test_autopush_pushes_after_shift(dut):
    """Rule 3: autopush pushes the ISR after the shift that reached the threshold."""
    await init_tile(dut)
    await run(dut, "c_autopush", shiftctrl=AUTOPUSH | push_thresh(8))
    await ClockCycles(dut.clk, 30)
    assert await drain(dut, 0) == [0x5A]


@cocotb.test()
async def test_autopush_full_stalls_without_loss(dut):
    """Rules 1/3: autopush on a full RX FIFO stalls; no word lost or repeated."""
    await init_tile(dut)
    await run(dut, "c_autopush_full", shiftctrl=AUTOPUSH)  # thresh 0 = 32
    got = []
    for _ in range(40):
        await ClockCycles(dut.clk, 30)
        got += await drain(dut, 0, limit=2)  # read slower than the program writes
        if len(got) >= 32:
            break
    assert got == list(range(31, -1, -1)), got


@cocotb.test()
async def test_autopull_refill_and_stall(dut):
    """Rule 3: autopull refills OSR; an OUT on an empty OSR and FIFO stalls."""
    await init_tile(dut)
    # PULL_THRESH 0 = 32: four bytes per word (with 8 each word would give one byte, as on the RP2040)
    await run(dut, "c_autopull", shiftctrl=AUTOPULL | pull_thresh(0) | OUT_RIGHT)
    await ClockCycles(dut.clk, 30)
    early = await drain(dut, 0)
    assert early == [], f"OUT ran with an empty OSR and FIFO: {early}"
    await wb_write(dut, SM_TXF(0), 0x44332211)
    await ClockCycles(dut.clk, 60)
    got = await drain(dut, 0)
    assert got == [0x11, 0x22, 0x33, 0x44], [hex(v) for v in got]
    await ClockCycles(dut.clk, 60)
    late = await drain(dut, 0)
    assert late == [], f"OUT did not stall after the OSR emptied: {late}"


@cocotb.test()
async def test_mov_osr_resets_count(dut):
    """Rule 4: `mov osr, x` makes the OSR full, so `jmp !osre` is taken."""
    await init_tile(dut)
    await run(dut, "c_mov_osr")
    await ClockCycles(dut.clk, 40)
    assert await drain(dut, 0) == [1]


@cocotb.test()
async def test_mov_isr_clears_count(dut):
    """Rule 4: `mov isr, null` clears the ISR count (autopush thresh 8)."""
    await init_tile(dut)
    await run(dut, "c_mov_isr", shiftctrl=AUTOPUSH | push_thresh(8))
    await ClockCycles(dut.clk, 40)
    assert await drain(dut, 0) == [0x33]


@cocotb.test()
async def test_mov_status(dut):
    """Rule 4: `mov isr, status` is all-ones when the TX level is below STATUS_N, else zero."""
    await init_tile(dut)
    await run(dut, "c_mov_status", execctrl=1)          # STATUS_SEL=TX, STATUS_N=1
    await ClockCycles(dut.clk, 20)
    assert await drain(dut, 0) == [0xFFFFFFFF]
    await run(dut, "c_mov_status", execctrl=1, enable=False)
    await wb_write(dut, SM_TXF(0), 0xAAAA)               # TX level 1: not below 1
    await wb_write(dut, REG_CTRL, 1)
    await ClockCycles(dut.clk, 20)
    assert await drain(dut, 0) == [0]


@cocotb.test()
async def test_sideset_opt_count_includes_enable(dut):
    """Rule 5: `.side_set 1 opt` (SIDESET_COUNT 2) drives the pin only when side-set is given."""
    await init_tile(dut)
    await run(dut, "c_sideset_opt", pinctrl=(3 << 10), enable=False)  # SIDESET_BASE=3
    await wb_write(dut, REG_CTRL, 1)
    seen = []
    for _ in range(12):
        await ClockCycles(dut.clk, 1)
        seen.append(pin(dut, 3))
    assert 1 in seen and seen[-1] == 0, seen
    first_one = seen.index(1)
    assert seen[first_one:first_one + 3] == [1, 1, 1], f"pin must stay 1 over the instructions without side-set: {seen}"


@cocotb.test()
async def test_irq_wait_waits_for_clear(dut):
    """Rule 6: `irq wait 0` sets flag 0 and waits until it is cleared."""
    await init_tile(dut)
    await run(dut, "c_irq_wait", pinctrl=SET_PIN0)
    await ClockCycles(dut.clk, 40)
    assert await wb_read(dut, REG_IRQ) & 1 == 1, "irq wait did not set the flag"
    assert pin(dut, 0) == 0, "irq wait did not wait"
    await wb_write(dut, REG_IRQ, 1)                      # host clears flag 0
    await ClockCycles(dut.clk, 20)
    assert pin(dut, 0) == 1


@cocotb.test()
async def test_irq_rel_keeps_bit2(dut):
    """Rule 6: `irq set 4 rel` on SM1 sets flag {1, (0+1)%4} = 5."""
    await init_tile(dut)
    await run(dut, "c_irq_rel", sm=1)
    await ClockCycles(dut.clk, 20)
    assert await wb_read(dut, REG_IRQ) & 0xFF == (1 << 5)


@cocotb.test()
async def test_wait_gpio(dut):
    """Rule 1: `wait 1 gpio 7` stalls until GPIO 7 is high."""
    await init_tile(dut)
    await run(dut, "c_wait_gpio", pinctrl=SET_PIN0)
    await ClockCycles(dut.clk, 40)
    assert pin(dut, 0) == 0
    dut.i_gpio.value = 1 << 7
    await ClockCycles(dut.clk, 20)
    assert pin(dut, 0) == 1


@cocotb.test()
async def test_restart_keeps_pc_fifo_pins(dut):
    """Rule 7 (datasheet CTRL.SM_RESTART): PC, X, the FIFOs and the pins survive a restart."""
    await init_tile(dut)
    await wb_write(dut, SM_PINCTRL(0), SET_PIN0)
    for ins in (0xE081, 0xE001, 0x0007, 0xE025):       # set pindirs 1; set pins 1; jmp 7; set x, 5
        await wb_write(dut, SM_INSTR(0), ins)
    await wb_write(dut, SM_TXF(0), 0xAB)
    await wb_write(dut, REG_CTRL, 1 << 4)               # restart SM0
    await ClockCycles(dut.clk, 4)
    assert await wb_read(dut, SM_STAT(0)) & 0x1F == 7, "restart moved the PC"
    assert int(dut.o_gpio_pindir.value) & 1 == 1 and pin(dut, 0) == 1, "restart changed the pin"
    for ins in (0x8080, 0xA0C7, 0x8000, 0xA0C1, 0x8000):  # pull noblock; mov isr, osr; push; mov isr, x; push
        await wb_write(dut, SM_INSTR(0), ins)
    await ClockCycles(dut.clk, 2)
    assert await drain(dut, 0) == [0xAB, 5], "restart lost the TX FIFO word or X"


@cocotb.test()
async def test_pin_latch_latest_writer_wins(dut):
    """Rule 10: one latch per pin for the tile; the latest write wins, whichever SM made it.
    (The per-SM latch let SM1, which once made pin 0 an input, keep it an input for ever.)"""
    await init_tile(dut)
    await wb_write(dut, SM_PINCTRL(0), SET_PIN0)
    await wb_write(dut, SM_PINCTRL(1), SET_PIN0)
    await wb_write(dut, SM_INSTR(1), 0xE080)            # SM1: set pindirs 0
    await wb_write(dut, SM_INSTR(0), 0xE001)            # SM0: set pins 1
    await wb_write(dut, SM_INSTR(0), 0xE081)            # SM0: set pindirs 1
    await ClockCycles(dut.clk, 2)
    assert int(dut.o_gpio_pindir.value) & 1 == 1 and pin(dut, 0) == 1, "SM0's later write lost to SM1"
    await wb_write(dut, SM_INSTR(1), 0xE000)            # SM1: set pins 0 (direction stays SM0's)
    await ClockCycles(dut.clk, 2)
    assert int(dut.o_gpio_pindir.value) & 1 == 1 and pin(dut, 0) == 0
    await wb_write(dut, REG_CTRL, 0x3 << 4)             # restarting both changes nothing on the pin
    await ClockCycles(dut.clk, 2)
    assert int(dut.o_gpio_pindir.value) & 1 == 1 and pin(dut, 0) == 0


@cocotb.test()
async def test_sm_instr_runs_while_disabled(dut):
    """SMx_INSTR executes immediately, also while the SM is disabled ([SPEC-PIO-05])."""
    await init_tile(dut)
    await wb_write(dut, SM_PINCTRL(0), SET_PIN0)
    await wb_write(dut, SM_INSTR(0), 0xE001)             # set pins, 1
    await ClockCycles(dut.clk, 5)
    assert pin(dut, 0) == 1
    await wb_write(dut, SM_INSTR(0), 0x0005)             # jmp 5
    await ClockCycles(dut.clk, 5)
    assert await wb_read(dut, SM_STAT(0)) & 0x1F == 5


FJOIN_TX, FJOIN_RX = 1 << 30, 1 << 31


async def tx_full(dut, sm):
    return (await wb_read(dut, REG_FIFO_STAT) >> (24 + sm)) & 1


@cocotb.test()
async def test_fifo_join_tx_and_flush(dut):
    """Rule 8: FJOIN_TX gives an 8-word TX FIFO; changing FJOIN empties the FIFOs."""
    await init_tile(dut)
    await wb_write(dut, SM_SHIFTCTRL(0), FJOIN_TX)
    for i in range(7):
        await wb_write(dut, SM_TXF(0), i)
        assert not await tx_full(dut, 0), f"full after {i + 1} words with FJOIN_TX"
    await wb_write(dut, SM_TXF(0), 7)
    assert await tx_full(dut, 0), "not full after 8 words"
    await wb_write(dut, SM_SHIFTCTRL(0), 0)                     # unjoin: flush
    for i in range(3):
        await wb_write(dut, SM_TXF(0), i)
        assert not await tx_full(dut, 0), "TX FIFO was not emptied by the FJOIN change"
    await wb_write(dut, SM_TXF(0), 3)
    assert await tx_full(dut, 0), "plain TX FIFO is not 4 deep"


@cocotb.test()
async def test_fifo_join_rx_8_words(dut):
    """Rule 8: FJOIN_RX keeps 8 pushed words, in order (pushes run through SMx_INSTR)."""
    await init_tile(dut)
    await wb_write(dut, SM_SHIFTCTRL(0), FJOIN_RX)
    for k in range(10):
        await wb_write(dut, SM_INSTR(0), 0xE020 | (k + 1))       # set x, k+1
        await wb_write(dut, SM_INSTR(0), 0xA0C1)                 # mov isr, x
        await wb_write(dut, SM_INSTR(0), 0x8000)                 # push noblock
    assert await drain(dut, 0, 12) == [1, 2, 3, 4, 5, 6, 7, 8]


@cocotb.test()
async def test_exec_stalled_latches_until_done(dut):
    """Rule 9: an SMx_INSTR `pull block` on an empty FIFO stays latched, EXEC_STALLED reads 1,
    and it completes when a word arrives (pio_sm_exec_wait_blocking)."""
    await init_tile(dut)
    await wb_write(dut, SM_INSTR(0), 0x80A0)                     # pull block, FIFO empty
    await ClockCycles(dut.clk, 10)
    assert await wb_read(dut, SM_EXECCTRL(0)) >> 31 == 1, "EXEC_STALLED not set"
    await wb_write(dut, SM_TXF(0), 0x1234_5678)
    await ClockCycles(dut.clk, 4)
    assert await wb_read(dut, SM_EXECCTRL(0)) >> 31 == 0, "EXEC_STALLED stuck"
    await wb_write(dut, SM_INSTR(0), 0xA0C7)                     # mov isr, osr
    await wb_write(dut, SM_INSTR(0), 0x8020)                     # push block
    await ClockCycles(dut.clk, 2)                                # SMx_INSTR runs 2 clocks after the write
    assert await drain(dut, 0) == [0x1234_5678]
