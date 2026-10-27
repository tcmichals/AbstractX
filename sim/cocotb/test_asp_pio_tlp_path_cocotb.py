# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generic per-tile TLP data path ([SPEC-PIO-10]) on asp_pio_tile.

Ingress: words dealt to the masked SMs (four at once in one clock), the 32-slot block-RAM queue,
backpressure only when the queue is full, other channels ignored. Egress: rounds from the masked
SMs, burst and idle-timeout sends, timestamp of the first word. Every SM runs an echo program
(pull -> mov isr, osr -> push), so what goes in comes back out. Both AXI-Stream sides use random
valid gaps / tready toggling and check that tdata stays stable while tvalid waits.

@verifies [SPEC-PIO-10] rtl/pio/SPECIFICATION.md
"""

import random

import cocotb
from cocotb.triggers import ClockCycles, FallingEdge, RisingEdge, Timer

from test_asp_pio_cocotb import (wb_write, wb_read, init_tile, REG_CTRL, IRAM_BASE, SM_CLKDIV,
                                 SM_EXECCTRL, SM_RXF, PIO_PULL, PIO_MOV, PIO_PUSH)

REG_OUT_CFG = 0x0C
REG_OUT_TIMEOUT = 0x90
REG_IN_CFG = 0x94
REG_IN_STATUS = 0x98
CH_IN = 0x40
CH_OUT = 0x41
SLOTS = 32
ECHO = [PIO_PULL(block=1), PIO_MOV(dest=6, op=0, src=7), PIO_PUSH(block=1)]


def make_tlp(mask, words, channel=CH_IN, length=None):
    length = len(words) if length is None else length
    v = ((0x02 << 24) | (mask << 16) | channel) << 480
    v |= (length << 16) << 416
    for k, w in enumerate(words[:10]):
        v |= (w & 0xFFFF_FFFF) << (320 - 32 * k)
    return v


def parse(raw):
    b = raw.to_bytes(64, "big")
    n = int.from_bytes(b[8:10], "big")
    return {
        "type": b[0], "mask": b[1], "tag": b[2], "channel": b[3],
        "addr": int.from_bytes(b[4:8], "big"), "len": n,
        "seq": int.from_bytes(b[10:12], "big"), "ts": int.from_bytes(b[12:20], "big"),
        "words": [int.from_bytes(b[20 + 4 * k:24 + 4 * k], "big") for k in range(n)],
        "pad": b[20 + 4 * n:60],
    }


class Egress:
    """Collects egress TLPs with random tready; checks tdata is stable while tvalid waits."""

    def __init__(self, dut, ready_prob=1.0):
        self.dut, self.ready_prob, self.pkts = dut, ready_prob, []
        cocotb.start_soon(self.run())

    async def run(self):
        held = None
        while True:
            await FallingEdge(self.dut.clk)
            valid = int(self.dut.m_tlp_tvalid.value)
            data = int(self.dut.m_tlp_tdata.value)
            if held is not None:
                assert valid and data == held, "egress tdata/tvalid changed before tready"
            ready = random.random() < self.ready_prob
            self.dut.m_tlp_tready.value = int(ready)
            if valid and ready:
                p = parse(data)
                if p["type"] == 0x10:
                    self.pkts.append(p)
                held = None
            else:
                held = data if valid else None

    async def wait_words(self, n, timeout_cycles=200_000):
        for _ in range(timeout_cycles // 100):
            if sum(p["len"] for p in self.pkts) >= n:
                return
            await ClockCycles(self.dut.clk, 100)
        raise AssertionError(f"egress: {sum(p['len'] for p in self.pkts)} of {n} words")


async def send_tlp(dut, tlp, gap_max=0):
    """AXI-Stream master: valid held with stable data until tready (sampled before the edge)."""
    for _ in range(random.randint(0, gap_max)):
        await FallingEdge(dut.clk)
    await FallingEdge(dut.clk)
    dut.s_tlp_tdata.value = tlp
    dut.s_tlp_tvalid.value = 1
    while True:
        await Timer(1, unit="ns")                   # let tready settle on the new tvalid
        accepted = int(dut.s_tlp_tready.value)
        await RisingEdge(dut.clk)
        if accepted:
            break
        await FallingEdge(dut.clk)
    await FallingEdge(dut.clk)
    dut.s_tlp_tvalid.value = 0


async def setup(dut, enable_sms=True):
    await init_tile(dut)
    for i, w in enumerate(ECHO):
        await wb_write(dut, IRAM_BASE + 4 * i, w)
    for s in range(4):
        await wb_write(dut, SM_EXECCTRL(s), 2 << 12)
        await wb_write(dut, SM_CLKDIV(s), 1 << 16)
    await wb_write(dut, REG_CTRL, 0xF << 4)
    await wb_write(dut, REG_IN_CFG, 1 | (CH_IN << 8))
    if enable_sms:
        await wb_write(dut, REG_CTRL, 0xF)


def out_cfg(mask, burst, tag=7, channel=CH_OUT):
    return 1 | (mask << 4) | (tag << 8) | (channel << 16) | (burst << 24)


@cocotb.test()
async def test_four_words_one_clock(dut):
    """mask 0xF, 4 words: all four TX FIFOs written in the same clock, word k to SM k."""
    await setup(dut, enable_sms=False)
    writes = []

    async def watch():
        while True:
            await RisingEdge(dut.clk)
            v = int(dut.u_tlp_ingress.o_tx_wvalid.value)
            if v:
                writes.append(v)
    cocotb.start_soon(watch())

    await send_tlp(dut, make_tlp(0xF, [0xA0, 0xA1, 0xA2, 0xA3]))
    await ClockCycles(dut.clk, 40)
    assert writes == [0xF], f"TX writes per clock: {[hex(w) for w in writes]}"

    await wb_write(dut, REG_CTRL, 0xF)
    await ClockCycles(dut.clk, 20)
    got = [await wb_read(dut, SM_RXF(s)) for s in range(4)]
    assert got == [0xA0, 0xA1, 0xA2, 0xA3], [hex(w) for w in got]


@cocotb.test()
async def test_partial_mask_groups(dut):
    """mask 0b1010, 5 words: groups (SM1, SM3), (SM1, SM3), (SM1); the rest of the slot is not sent."""
    await setup(dut)
    await send_tlp(dut, make_tlp(0b1010, [1, 2, 3, 4, 5, 0xDEAD, 0xBEEF], length=5))
    await ClockCycles(dut.clk, 100)
    sm1 = [await wb_read(dut, SM_RXF(1)) for _ in range(3)]
    sm3 = [await wb_read(dut, SM_RXF(3)) for _ in range(2)]
    assert sm1 == [1, 3, 5] and sm3 == [2, 4], (sm1, sm3)
    stat = await wb_read(dut, 0x08)
    assert (stat >> 28) == 0xF, f"RX FIFOs not empty afterwards: 0x{stat:08X}"


@cocotb.test()
async def test_other_channel_and_disabled_ignored(dut):
    await setup(dut)
    await send_tlp(dut, make_tlp(0x1, [0x55], channel=CH_IN + 1))
    await wb_write(dut, REG_IN_CFG, 0)
    await send_tlp(dut, make_tlp(0x1, [0x66]))
    await send_tlp(dut, make_tlp(0x0, [0x77]))                 # empty mask: consumed, nothing written
    await ClockCycles(dut.clk, 50)
    assert (await wb_read(dut, 0x08) >> 28) == 0xF, "a word was delivered"


@cocotb.test()
async def test_queue_full_backpressure_and_stream_order(dut):
    """SM0 stopped: 4 words fit the TX FIFO, the dealt slot waits, 32 slots queue; the 33rd TLP
    waits on tready (never dropped). Then SM0 runs and every word comes back in order through
    egress streaming (mask 0x1, burst 10) under random tready."""
    await setup(dut, enable_sms=False)
    words = [0x1000 + i for i in range(10 * (SLOTS + 2))]
    tlps = [make_tlp(0x1, words[10 * i:10 * i + 10]) for i in range(SLOTS + 2)]
    for t in tlps[:SLOTS]:
        await send_tlp(dut, t)
    await ClockCycles(dut.clk, 30)
    stat = await wb_read(dut, REG_IN_STATUS)
    assert stat & 0x3F == SLOTS and (stat >> 8) & 1, f"IN_STATUS 0x{stat:08X}"

    # next TLP is held, not dropped
    dut.s_tlp_tdata.value = tlps[SLOTS]
    dut.s_tlp_tvalid.value = 1
    for _ in range(50):
        await FallingEdge(dut.clk)
        await Timer(1, unit="ns")
        assert int(dut.s_tlp_tready.value) == 0, "accepted with a full queue"
    dut.s_tlp_tvalid.value = 0

    eg = Egress(dut, ready_prob=0.4)
    await wb_write(dut, REG_OUT_CFG, out_cfg(0x1, 10))
    await wb_write(dut, REG_CTRL, 0x1)
    for t in tlps[SLOTS:]:
        await send_tlp(dut, t)
    await eg.wait_words(len(words))
    got = [w for p in eg.pkts for w in p["words"]]
    assert got == words, "stream order / loss"
    assert all(p["mask"] == 1 and p["tag"] == 7 and p["channel"] == CH_OUT and
               p["addr"] == 0x40000700 for p in eg.pkts)
    assert [p["seq"] for p in eg.pkts] == list(range(len(eg.pkts)))
    assert all(p["len"] == 10 for p in eg.pkts)
    assert (await wb_read(dut, REG_IN_STATUS)) == 0


@cocotb.test()
async def test_gather_rounds_dshot_layout(dut):
    """mask 0xF, burst 8: two rounds of four in one TLP, SM order; random ingress gaps and tready."""
    await setup(dut)
    eg = Egress(dut, ready_prob=0.5)
    await wb_write(dut, REG_OUT_CFG, out_cfg(0xF, 8))
    expect = []
    for frame in range(20):
        a = [(frame << 8) | (0x10 * s) for s in range(4)]
        b = [(frame << 8) | (0x10 * s + 1) for s in range(4)]
        expect.append(a + b)
        await send_tlp(dut, make_tlp(0xF, a), gap_max=20)
        await send_tlp(dut, make_tlp(0xF, b), gap_max=20)
    await eg.wait_words(8 * 20)
    assert [p["words"] for p in eg.pkts] == expect
    assert all(p["mask"] == 0xF and p["len"] == 8 and p["pad"] == bytes(len(p["pad"])) for p in eg.pkts)


@cocotb.test()
async def test_idle_timeout_and_timestamp(dut):
    """3 words with burst 10: no TLP without a timeout; with OUT_TIMEOUT a short TLP carrying the
    timestamp of the clock its first word left the RX FIFO."""
    await setup(dut)
    eg = Egress(dut)
    await wb_write(dut, REG_OUT_CFG, out_cfg(0x1, 10))

    async def clock_ts():
        t = 0
        while True:
            await RisingEdge(dut.clk)
            t += 1
            dut.i_sys_timestamp.value = t
    cocotb.start_soon(clock_ts())

    first_pop = []

    async def watch_pop():
        while True:
            await RisingEdge(dut.clk)
            if int(dut.u_tlp_shim.o_rx_pop.value) and not first_pop:
                first_pop.append(int(dut.i_sys_timestamp.value))
    cocotb.start_soon(watch_pop())

    await send_tlp(dut, make_tlp(0x1, [7, 8, 9]))
    await ClockCycles(dut.clk, 2000)
    assert eg.pkts == [], "sent before burst without a timeout"

    await wb_write(dut, REG_OUT_TIMEOUT, 100)
    await eg.wait_words(3, 2000)
    p = eg.pkts[0]
    assert p["words"] == [7, 8, 9] and p["len"] == 3
    assert first_pop and abs(p["ts"] - first_pop[0]) <= 1, (p["ts"], first_pop)


@cocotb.test()
async def test_trace_and_data_share_egress(dut):
    """Trace events (software trigger) between data TLPs under random tready: a presented TLP
    stays unchanged until accepted (checked by Egress) and no data word is lost."""
    await setup(dut)
    eg = Egress(dut, ready_prob=0.3)
    await wb_write(dut, REG_OUT_CFG, out_cfg(0x1, 1) | (1 << 31))
    sent = list(range(0x200, 0x200 + 40))
    for i in range(0, len(sent), 2):
        await send_tlp(dut, make_tlp(0x1, sent[i:i + 2]), gap_max=3)
        await wb_write(dut, 0x11C, 1)                 # SM0 trigger: trace event
    await eg.wait_words(len(sent))
    assert [w for p in eg.pkts for w in p["words"]] == sent
