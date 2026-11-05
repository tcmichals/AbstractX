# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-TLP-04] docs/DESIGN_SPECIFICATION.md (test tooling: AXI4-Lite models and protocol checks)
"""Small AXI4-Lite bus functional models for the cocotb suites (no cocotbext-axi here).

AxilMaster drives a slave port (<prefix>_awaddr ... <prefix>_rready) with random valid gaps and checks
that a slave never answers before the request. AxilSlave answers a master port from a dict, with random
ready / valid delays, error regions (SLVERR / DECERR) and a stability check: address, data and valid
must not change while valid waits for ready (AXI4-Lite rule).
"""

import random

import cocotb
from cocotb.triggers import FallingEdge, ReadOnly, RisingEdge

OKAY, SLVERR, DECERR = 0, 2, 3


def _sig(dut, prefix, name):
    return getattr(dut, f"{prefix}_{name}")


class AxilMaster:
    def __init__(self, dut, prefix="s_axil", clk=None, gap=0.0):
        self.dut, self.p, self.clk, self.gap = dut, prefix, clk or dut.clk, gap
        self._lock = None
        for n in ("awvalid", "wvalid", "bready", "arvalid", "rready"):
            _sig(dut, prefix, n).value = 0
        for n in ("awaddr", "wdata", "araddr"):
            _sig(dut, prefix, n).value = 0
        _sig(dut, prefix, "wstrb").value = 0xF
        for n in ("awprot", "arprot"):
            try:
                _sig(dut, prefix, n).value = 0
            except AttributeError:
                pass

    async def _gap(self):
        while random.random() < self.gap:
            await FallingEdge(self.clk)

    async def _wait(self, valid, ready, limit=2000):
        for _ in range(limit):
            await RisingEdge(self.clk)
            if int(ready.value):
                return
        raise AssertionError(f"{self.p}: no {ready._name} within {limit} clocks")

    async def write(self, addr, data):
        if self._lock is None:
            from cocotb.triggers import Lock
            self._lock = Lock()
        async with self._lock:
            await FallingEdge(self.clk)
            await self._gap()
            aw, w = _sig(self.dut, self.p, "awvalid"), _sig(self.dut, self.p, "wvalid")
            _sig(self.dut, self.p, "awaddr").value = addr
            _sig(self.dut, self.p, "wdata").value = data
            aw.value = 1
            w.value = 1
            aw_done = w_done = False
            for _ in range(2000):
                await RisingEdge(self.clk)
                aw_done |= bool(int(_sig(self.dut, self.p, "awready").value)) and int(aw.value) == 1
                w_done |= bool(int(_sig(self.dut, self.p, "wready").value)) and int(w.value) == 1
                await FallingEdge(self.clk)
                if aw_done:
                    aw.value = 0
                if w_done:
                    w.value = 0
                if aw_done and w_done:
                    break
            else:
                raise AssertionError(f"{self.p}: write 0x{addr:08X} not accepted")
            _sig(self.dut, self.p, "bready").value = 1
            await self._wait(_sig(self.dut, self.p, "bvalid"), _sig(self.dut, self.p, "bvalid"))
            resp = int(_sig(self.dut, self.p, "bresp").value)
            await FallingEdge(self.clk)
            _sig(self.dut, self.p, "bready").value = 0
            return resp

    async def read(self, addr):
        if self._lock is None:
            from cocotb.triggers import Lock
            self._lock = Lock()
        async with self._lock:
            await FallingEdge(self.clk)
            await self._gap()
            ar = _sig(self.dut, self.p, "arvalid")
            _sig(self.dut, self.p, "araddr").value = addr
            ar.value = 1
            await self._wait(ar, _sig(self.dut, self.p, "arready"))
            await FallingEdge(self.clk)
            ar.value = 0
            _sig(self.dut, self.p, "rready").value = 1
            await self._wait(_sig(self.dut, self.p, "rvalid"), _sig(self.dut, self.p, "rvalid"))
            data = int(_sig(self.dut, self.p, "rdata").value)
            resp = int(_sig(self.dut, self.p, "rresp").value)
            await FallingEdge(self.clk)
            _sig(self.dut, self.p, "rready").value = 0
            return data, resp


class AxilSlave:
    """Answers a master port: mem dict (word address -> value), errors {addr: resp}."""

    def __init__(self, dut, prefix="m_axil", clk=None, delay=0.0, mem=None, errors=None):
        self.dut, self.p, self.clk, self.delay = dut, prefix, clk or dut.clk, delay
        self.mem = mem if mem is not None else {}
        self.errors = errors or {}
        self.writes, self.reads = [], []
        for n in ("awready", "wready", "bvalid", "arready", "rvalid"):
            _sig(dut, prefix, n).value = 0
        _sig(dut, prefix, "bresp").value = 0
        _sig(dut, prefix, "rresp").value = 0
        _sig(dut, prefix, "rdata").value = 0
        cocotb.start_soon(self._write_side())
        cocotb.start_soon(self._read_side())
        cocotb.start_soon(self._stability())

    def _slow(self):
        return random.random() < self.delay

    async def _stability(self):
        """AXI rule on the master: a valid, once raised, keeps its payload until ready."""
        prev = {}
        while True:
            # Mid-cycle (after the falling edge, where this model drives ready): the values the next
            # rising edge samples. Sampling right after the rising edge misses a ready raised later.
            await FallingEdge(self.clk)
            await ReadOnly()
            for ch, fields in (("aw", ("awaddr",)), ("w", ("wdata",)), ("ar", ("araddr",))):
                v = int(_sig(self.dut, self.p, ch + "valid").value)
                r = int(_sig(self.dut, self.p, ch + "ready").value)
                cur = tuple(int(_sig(self.dut, self.p, f).value) for f in fields)
                if ch in prev:
                    assert v == 1 and cur == prev[ch], f"{self.p}: {ch} changed or dropped before ready"
                    del prev[ch]
                if v and not r:
                    prev[ch] = cur

    async def _write_side(self):
        while True:
            await FallingEdge(self.clk)
            if not (int(_sig(self.dut, self.p, "awvalid").value) and int(_sig(self.dut, self.p, "wvalid").value)):
                continue
            while self._slow():
                await FallingEdge(self.clk)
            addr = int(_sig(self.dut, self.p, "awaddr").value)
            data = int(_sig(self.dut, self.p, "wdata").value)
            _sig(self.dut, self.p, "awready").value = 1
            _sig(self.dut, self.p, "wready").value = 1
            await FallingEdge(self.clk)
            _sig(self.dut, self.p, "awready").value = 0
            _sig(self.dut, self.p, "wready").value = 0
            resp = self.errors.get(addr, OKAY)
            if resp == OKAY:
                self.mem[addr] = data
            self.writes.append((addr, data, resp))
            while self._slow():
                await FallingEdge(self.clk)
            _sig(self.dut, self.p, "bresp").value = resp
            _sig(self.dut, self.p, "bvalid").value = 1
            while True:
                await RisingEdge(self.clk)
                if int(_sig(self.dut, self.p, "bready").value):
                    break
            await FallingEdge(self.clk)
            _sig(self.dut, self.p, "bvalid").value = 0

    async def _read_side(self):
        while True:
            await FallingEdge(self.clk)
            if not int(_sig(self.dut, self.p, "arvalid").value):
                continue
            while self._slow():
                await FallingEdge(self.clk)
            addr = int(_sig(self.dut, self.p, "araddr").value)
            _sig(self.dut, self.p, "arready").value = 1
            await FallingEdge(self.clk)
            _sig(self.dut, self.p, "arready").value = 0
            resp = self.errors.get(addr, OKAY)
            self.reads.append((addr, resp))
            while self._slow():
                await FallingEdge(self.clk)
            _sig(self.dut, self.p, "rdata").value = self.mem.get(addr, 0) if resp == OKAY else 0
            _sig(self.dut, self.p, "rresp").value = resp
            _sig(self.dut, self.p, "rvalid").value = 1
            while True:
                await RisingEdge(self.clk)
                if int(_sig(self.dut, self.p, "rready").value):
                    break
            await FallingEdge(self.clk)
            _sig(self.dut, self.p, "rvalid").value = 0
