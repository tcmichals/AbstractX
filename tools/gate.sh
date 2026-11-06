#!/usr/bin/env bash
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-AUDIT-17] docs/SPECTRACE.md (gate)
#
# The AGENTS §10.2 gate: host suite plain, ASan/UBSan and TSan, then static analysis and the boundary
# review (§10.9). A build error stops the run before ctest, so a stale binary can never pass for a test
# that no longer compiles.
#   tools/gate.sh            all three
#   tools/gate.sh --quick    plain only (pre-push)
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

JOBS=$(nproc 2>/dev/null || echo 4)
B=${GATE_BUILD_PREFIX:-build}  # tests point this at a temp dir so they never touch build_*
QUICK=0
[ "${1:-}" = "--quick" ] && QUICK=1

run_suite() {
    local name=$1 dir=$2 prefix=$3
    shift 3
    echo "== gate: $name ($dir)"
    mkdir -p "$dir"
    cmake -S . -B "$dir" -DABSTRACTX_TARGET=host "$@" >/dev/null
    if ! cmake --build "$dir" -j"$JOBS" >"$dir/gate_build.log" 2>&1; then
        grep -E "error|Error" "$dir/gate_build.log" | head -20
        echo "[FAIL] gate: $name build failed (full log: $dir/gate_build.log); no tests run"
        exit 1
    fi
    if ! timeout 2400 $prefix ctest --test-dir "$dir" -j"$JOBS" --timeout 60 --output-on-failure \
            >"$dir/gate_ctest.log" 2>&1; then
        grep -E "tests passed|^\s+[0-9]+ - " "$dir/gate_ctest.log"
        echo "[FAIL] gate: $name tests (full log: $dir/gate_ctest.log)"
        exit 1
    fi
    echo "   $(grep -E 'tests passed' "$dir/gate_ctest.log")"
}

run_suite plain "${B}_host" ""
if [ "$QUICK" = 0 ]; then
    # RTL suites (label rtl) run in the plain build only: sanitizer flags do not reach Verilator.
    run_suite asan "${B}_asan" "" -DABSTRACTX_RTL_TESTS=OFF \
        -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer" \
        -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
    # ASLR off: else TSan may abort at start ("unexpected memory mapping"), which is not a race
    run_suite tsan "${B}_tsan" "setarch -R" -DABSTRACTX_RTL_TESTS=OFF \
        -DCMAKE_CXX_FLAGS="-fsanitize=thread -O1 -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
fi
if [ "$QUICK" = 0 ]; then
    # Review layers ([SPEC-AUDIT-17] review layers in the gate, AGENTS §10.9)
    echo "== gate: boundary review"
    if ! timeout 300 python3 tools/boundary_review.py --check >"${B}_host/gate_boundary.log" 2>&1; then
        tail -20 "${B}_host/gate_boundary.log"
        echo "[FAIL] gate: boundary review (full log: ${B}_host/gate_boundary.log)"
        exit 1
    fi
    echo "   $(grep -E 'Shared state' "${B}_host/gate_boundary.log")"
    echo "== gate: static analysis"
    if ! command -v clang-tidy >/dev/null 2>&1 && ! ls /usr/bin/clang-tidy-* >/dev/null 2>&1; then
        echo "   [SKIP] static analysis: clang-tidy not installed"
    elif ! timeout 900 python3 tools/run_static_analysis.py --build-dir "${B}_host" >"${B}_host/gate_static.log" 2>&1; then
        tail -20 "${B}_host/gate_static.log"
        echo "[FAIL] gate: static analysis (full log: ${B}_host/gate_static.log)"
        exit 1
    else
        echo "   $(tail -1 "${B}_host/gate_static.log")"
    fi
fi
echo "[PASS] gate$([ "$QUICK" = 1 ] && echo ' (quick: plain only)')"
