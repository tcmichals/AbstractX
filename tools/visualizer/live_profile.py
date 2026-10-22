# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-STUDIO-11] [SPEC-STUDIO-17] tools/visualizer/SPECIFICATION.md
"""Live profiler and timeline state for Studio (no GUI code; tested on its own).

Symbolizer   PC -> (function, repo-relative file, line) with the firmware ELF (addr2line),
             resolved on a background thread and cached.
ProfileWindow  the last few seconds of PC samples ([SPEC-TRACE-09]) aggregated per line,
             per function and per file, with which coroutine owned each sample.
TimelineWindow the last spans ([SPEC-TRACE-10]) on the target's cycle clock.
"""
import collections
import glob
import os
import queue
import shutil
import subprocess
import threading
import time

OWNER_NONE, OWNER_IDLE = 0, 1


def find_addr2line():
    for pattern in (os.path.expanduser("~/.tools/xpack-riscv-none-elf-gcc-*/bin/riscv-none-elf-addr2line"),):
        hits = sorted(glob.glob(pattern))
        if hits:
            return hits[-1]
    return shutil.which("riscv-none-elf-addr2line")


def normalize_path(path, repo_root):
    """addr2line's file name -> repo-relative path ('' when unknown)."""
    if not path or path.startswith("??"):
        return ""
    path = os.path.normpath(path)
    root = os.path.normpath(str(repo_root))
    if path.startswith(root + os.sep):
        return os.path.relpath(path, root)
    return path


def parse_location(text):
    """'file:123 (discriminator 2)' -> ('file', 123); '??:0' / '??:?' -> ('', 0)."""
    text = text.split(" (discriminator")[0].strip()
    file, _, line = text.rpartition(":")
    try:
        return file, int(line)
    except ValueError:
        return "", 0


class Symbolizer:
    """PC -> (function, file, line), cached; unknown PCs are resolved in the background."""

    def __init__(self, elf_path, repo_root, addr2line=None, runner=None):
        self.elf_path = str(elf_path)
        self.repo_root = repo_root
        self.cache = {}
        self._pending = set()
        self._queue = queue.Queue()
        self._lock = threading.Lock()
        self._runner = runner  # tests: callable(list of pcs) -> list of (func, "file:line")
        self.error = ""
        if runner is None:
            tool = addr2line or find_addr2line()
            if not tool:
                self.error = "riscv-none-elf-addr2line not found"
            elif not os.path.exists(self.elf_path):
                self.error = f"firmware ELF not found: {self.elf_path}"
            else:
                self._runner = self._make_addr2line(tool)
        if self._runner:
            threading.Thread(target=self._worker, daemon=True).start()

    def _make_addr2line(self, tool):
        proc = subprocess.Popen([tool, "-f", "-C", "-e", self.elf_path], stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, bufsize=1)

        def run(pcs):
            out = []
            for pc in pcs:
                proc.stdin.write(f"0x{pc:x}\n")
                proc.stdin.flush()
                func = proc.stdout.readline().strip()
                loc = proc.stdout.readline().strip()
                out.append((func, loc))
            return out
        return run

    def _worker(self):
        while True:
            pcs = [self._queue.get()]
            while not self._queue.empty() and len(pcs) < 256:
                pcs.append(self._queue.get_nowait())
            try:
                results = self._runner(pcs)
            except Exception as e:  # noqa: BLE001 - shown in Studio, never crashes the UI
                self.error = f"addr2line failed: {e}"
                return
            with self._lock:
                for pc, (func, loc) in zip(pcs, results):
                    file, line = parse_location(loc)
                    self.cache[pc] = (func if func and func != "??" else f"0x{pc:08x}",
                                      normalize_path(file, self.repo_root), line)
                    self._pending.discard(pc)

    def lookup(self, pc):
        """(function, file, line) or None while it is being resolved."""
        hit = self.cache.get(pc)
        if hit is None and self._runner and pc not in self._pending:
            with self._lock:
                self._pending.add(pc)
            self._queue.put(pc)
        return hit

    def wait_idle(self, timeout=5.0):
        end = time.time() + timeout
        while time.time() < end:
            with self._lock:
                if not self._pending:
                    return True
            time.sleep(0.01)
        return False


class ProfileWindow:
    """The last `window_s` seconds of PC samples."""

    def __init__(self, window_s=5.0, maxlen=20000):
        self.window_s = window_s
        self.samples = collections.deque(maxlen=maxlen)  # (host time, pc, owner)
        self.sample_hz = 0
        self.dropped = 0
        self.packets = 0

    def add(self, packet, now=None):
        now = time.time() if now is None else now
        self.sample_hz = packet["sample_hz"]
        self.dropped = packet["dropped"]
        self.packets += 1
        for pc, owner in packet["samples"]:
            self.samples.append((now, pc, owner))

    def aggregate(self, symbolizer, owner_names=None, now=None):
        """Counts over the window: per (file, line), per function, per file, per owner."""
        now = time.time() if now is None else now
        owner_names = owner_names or {}
        lines = collections.defaultdict(lambda: {"n": 0, "owners": collections.Counter()})
        funcs = collections.Counter()
        func_where = {}
        files = collections.Counter()
        owners = collections.Counter()
        total = unresolved = 0
        for t, pc, owner in list(self.samples):
            if now - t > self.window_s:
                continue
            total += 1
            name = ("idle" if owner == OWNER_IDLE else "loop" if owner == OWNER_NONE
                    else owner_names.get(owner, f"0x{owner:08x}"))
            owners[name] += 1
            sym = symbolizer.lookup(pc) if symbolizer else None
            if sym is None:
                unresolved += 1
                continue
            func, file, line = sym
            funcs[func] += 1
            func_where.setdefault(func, (file, line))
            if file:
                files[file] += 1
                entry = lines[(file, line)]
                entry["n"] += 1
                entry["owners"][name] += 1
        return {"total": total, "unresolved": unresolved, "lines": dict(lines),
                "funcs": [(f, n, *func_where[f]) for f, n in funcs.most_common()],
                "files": files, "owners": owners}


class TimelineWindow:
    """Spans on the target cycle clock; keeps the newest `maxlen`."""

    def __init__(self, maxlen=40000):
        self.spans = collections.deque(maxlen=maxlen)  # (start, end, id, kind)
        self.cycle_hz = 0
        self.cycles_now = 0
        self.dropped = 0
        self.packets = 0

    def add(self, packet):
        self.cycle_hz = packet["cycle_hz"]
        self.cycles_now = max(self.cycles_now, packet["cycles_now"])
        self.dropped = packet["dropped"]
        self.packets += 1
        for s in packet["spans"]:
            self.spans.append((s["start"], s["start"] + s["duration"], s["id"], s["kind"]))

    def window(self, length_cycles, end=None):
        """Spans overlapping [end - length, end], oldest first."""
        end = self.cycles_now if end is None else end
        begin = end - length_cycles
        return [s for s in list(self.spans) if s[1] >= begin and s[0] <= end]
