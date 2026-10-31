#!/usr/bin/env python3
# @verifies [SPEC-BUILD-01] docs/DESIGN_SPECIFICATION.md
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""Toolchain files find the compiler on any PC: -D value, env, newest ~/.tools dir, PATH."""

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TC = REPO / "cmake" / "toolchains"

# (file, prefix variable, ~/.tools dir names oldest->newest, gcc name, expected prefix tail)
CASES = [
    ("riscv-none-elf.cmake", "RISCV_TOOLCHAIN_PREFIX",
     ["xpack-riscv-none-elf-gcc-14.2.0-1", "xpack-riscv-none-elf-gcc-15.2.0-1"],
     "riscv-none-elf-gcc", "riscv-none-elf-"),
    ("arm-none-eabi.cmake", "ARM_TOOLCHAIN_PREFIX",
     ["xpack-arm-none-eabi-gcc-14.2.1-1.1", "xpack-arm-none-eabi-gcc-15.2.1-1.1"],
     "arm-none-eabi-gcc", "arm-none-eabi-"),
    ("xtensa-esp-elf.cmake", "XTENSA_TOOLCHAIN_PREFIX",
     ["xtensa-esp-elf"], "xtensa-esp32-elf-gcc", "xtensa-esp32-elf-"),
]

PROBE = 'include(${TC})\nmessage(STATUS "PREFIX=${${VAR}}")\n'


def _prefix(tc: Path, var: str, home: Path, env_value=None, define=None) -> str:
    env = {k: v for k, v in os.environ.items() if not k.endswith("_TOOLCHAIN_PREFIX")}
    env["HOME"] = str(home)
    if env_value is not None:
        env[var] = env_value
    with tempfile.NamedTemporaryFile("w", suffix=".cmake", delete=False) as f:
        f.write(PROBE)
        probe = f.name
    try:
        cmd = ["cmake", f"-DTC={tc}", f"-DVAR={var}"]
        if define is not None:
            cmd.append(f"-D{var}={define}")
        out = subprocess.run(cmd + ["-P", probe], env=env, capture_output=True, text=True, check=True)
    finally:
        os.unlink(probe)
    m = re.search(r"PREFIX=(.*)", out.stdout + out.stderr)
    assert m, out.stdout + out.stderr
    return m.group(1).strip()


def test_no_absolute_home_paths():
    for name, *_ in CASES:
        text = (TC / name).read_text(encoding="utf-8")
        assert "/home/" not in text, f"{name} hardcodes a home directory"


def test_search_order():
    for name, var, dirs, gcc, tail in CASES:
        with tempfile.TemporaryDirectory() as td:
            home = Path(td)
            for d in dirs:
                b = home / ".tools" / d / "bin"
                b.mkdir(parents=True)
                (b / gcc).write_text("", encoding="utf-8")
            newest = f"{home}/.tools/{dirs[-1]}/bin/{tail}"
            assert _prefix(TC / name, var, home) == newest, name
            assert _prefix(TC / name, var, home, env_value="/env/p-") == "/env/p-", name
            assert _prefix(TC / name, var, home, env_value="/env/p-", define="/def/p-") == "/def/p-", name


if __name__ == "__main__":
    if shutil.which("cmake") is None:
        print("SKIP: cmake not on PATH")
        sys.exit(0)
    test_no_absolute_home_paths()
    test_search_order()
    print("toolchain file tests passed")
