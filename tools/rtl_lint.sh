#!/usr/bin/env bash
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# rtl-lint skill: Verilator -Wall lint of every first-party RTL file as its own top ([SPEC-AUDIT-15]).
# Exit 1 if any file fails. The pio2 submodule's own RTL is linted in pio2; here only through the
# tops that use it. Usage: tools/rtl_lint.sh [file.sv ...]
set -uo pipefail
cd "$(dirname "$0")/.."
shopt -s globstar nullglob
[ -f ~/.tools/oss-cad-suite/environment ] && source ~/.tools/oss-cad-suite/environment
command -v verilator >/dev/null || { echo "verilator not found: SKIPPED (not a pass)"; exit 2; }

INCS=(-Irtl -Irtl/spi -Irtl/pio_tlp -Irtl/pio/src/rtl -Irtl/pio/third_party/verilog-axi/rtl
      -Irtl/pio/third_party/verilog-axis/rtl -Ihw/zynq7000/qmtech_zynq7020/rtl)
PKG=(rtl/asp_tlp_pkg.sv)   # packages are not found through -I: always read first ([SPEC-TLP-04])
WAIVE=(-Wno-DECLFILENAME -Wno-UNUSEDSIGNAL -Wno-UNUSEDPARAM -Wno-PINCONNECTEMPTY -Wno-WIDTHTRUNC -Wno-WIDTHEXPAND)
CFG=(rtl/pio/src/rtl/third_party.vlt rtl/pio_tlp/asp_pio_wrap.vlt)   # waivers with reasons in each file

files=("$@")
[ ${#files[@]} -eq 0 ] && files=(rtl/*.sv rtl/*/*.sv hw/**/rtl/*.sv)
fail=0; n=0
for f in "${files[@]}"; do
    case "$f" in rtl/pio/*|rtl/asp_tlp_pkg.sv) continue ;; esac
    n=$((n + 1))
    out=$(timeout 60 verilator --lint-only -Wall --timescale 1ns/1ps "${WAIVE[@]}" "${INCS[@]}"  "${CFG[@]}" "${PKG[@]}" "$f" \
          --top-module "$(basename "$f" .sv)" 2>&1)
    if [ $? -ne 0 ]; then
        fail=$((fail + 1)); echo "FAIL $f"
        echo "$out" | grep -E "%(Error|Warning)" | grep -v "third_party/" | head -8 | sed 's/^/     /'
    else
        echo "ok   $f"
    fi
done
echo "rtl_lint: $((n - fail))/$n files clean"
[ $fail -eq 0 ]
