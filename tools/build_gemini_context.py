#!/usr/bin/env python3
"""
build_gemini_context.py - AbstractX Complete Architecture Context Bundler

Generates a single, comprehensive text bundle (`abstractx_context_for_gemini.txt`)
containing all authoritative documentation, architecture specifications, C++ headers,
target BSPs, RTL cores, visualizer source files, and test suites for Gemini Pro
architectural analysis and review.
"""

import os
import glob
import sys

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUTPUT_FILE = os.path.join(REPO_ROOT, "abstractx_context_for_gemini.txt")

FILE_PATTERNS = [
    # Top-Level Root Documents
    "README.md",
    "AGENTS.md",
    "docs/README.md",
    "docs/DESIGN_SPECIFICATION.md",
    "docs/SPEC_TEMPLATE.md",

    # Tier 1 Vision & Architectural Prompts
    "docs/tier1_vision/*.md",

    # Tier 2 System Contracts, Protocols & Observability
    "docs/tier2_contracts/*.md",
    "docs/tier2_contracts/observability/*.md",

    # Tier 3 Silicon Targets & Peripherals
    "docs/tier3_targets/**/*.md",

    # Verification & Engineering Evidence
    "docs/verification/*.md",
    "docs/verification/evidence/validation/*.md",
    "docs/verification/evidence/validation/*.json",

    # Developer Tools & Automated Validation
    "tools/audit_specs.py",
    "tools/run_adversarial_audit.py",
    "tools/track_memory_membrowse.py",
    "tools/create_app_spec.py",
    "tools/setup_venv.sh",
    "tools/membrowse-targets.json",

    # AbstractX Studio & Visualizer Suite
    "tools/visualizer/SPECIFICATION.md",
    "tools/visualizer/abstractx_studio.py",
    "tools/visualizer/ctf_schema_loader.py",
    "tools/visualizer/flight_plugin.py",
    "tools/visualizer/memory_metrics.json",
    "trace/barectf_config.yaml",

    # Applications
    "apps/README.md",
    "apps/gps_imu_app/README.md",
    "apps/gps_imu_app/SPECIFICATION.md",
    "apps/gps_imu_app/trace_schema.json",
    "apps/gps_imu_app/src/main.cpp",
    "apps/gps_imu_app/tools/flight_display.py",
    "apps/gps_imu_app/platforms/**/*.md",
    "apps/gps_imu_app/platforms/**/*.cpp",

    # Silicon Targets & BSPs
    "targets/SPECIFICATION.md",
    "targets/allwinner_e907/SPECIFICATION.md",
    "targets/allwinner_e907/include/**/*",
    "targets/allwinner_e907/src/**/*",
    "targets/allwinner_e907/bsp/resource_table.c",
    "targets/esp32p4/SPECIFICATION.md",
    "targets/esp32p4/io_processor.yaml",
    "targets/esp32p4/src/**/*",
    "targets/linux/SPECIFICATION.md",
    "targets/linux/io_processor.yaml",
    "targets/linux/include/**/*",
    "targets/linux/src/**/*",
    "targets/pico2w_rp2350/SPECIFICATION.md",
    "targets/pico2w_rp2350/HOWTO.md",
    "targets/pico2w_rp2350/io_processor.yaml",
    "targets/pico2w_rp2350/include/**/*",
    "targets/pico2w_rp2350/src/**/*",

    # Core Include Headers (C++20 Freestanding HAL, Coroutine Engine, Drivers)
    "include/abstractx/**/*.hpp",
    "include/*.h",
    "include/*.hpp",

    # Hardware SystemVerilog RTL Cores
    "rtl/**/*.sv",
    "rtl/*.sv",

    # Verification Test Suites
    "tests/test_visualizer_sdk.py",
    "tests/test_spec_traceability.py",
    "tests/test_membrowse_tracking.py",
    "tests/test_adversarial_invariants.py",
    "tests/test_tlp_and_ctf_schema.py"
]


def build_context():
    os.chdir(REPO_ROOT)
    collected_files = set()

    for pattern in FILE_PATTERNS:
        for match in glob.glob(pattern, recursive=True):
            if os.path.isfile(match):
                collected_files.add(os.path.relpath(match, REPO_ROOT))

    sorted_files = sorted(list(collected_files))
    print(f"[build_gemini_context] Collecting {len(sorted_files)} authoritative files...")

    total_bytes = 0
    with open(OUTPUT_FILE, "w", encoding="utf-8") as out:
        out.write("# ABSTRACTX COMPLETE ARCHITECTURE CONTEXT FOR GEMINI PRO\n")
        out.write("# Auto-generated bundle containing full specifications, code headers, target configs, and tools.\n\n")

        for rel_path in sorted_files:
            try:
                with open(rel_path, "r", encoding="utf-8", errors="replace") as f:
                    content = f.read()
                out.write("=" * 80 + "\n")
                out.write(f"FILE: {rel_path}\n")
                out.write("=" * 80 + "\n")
                out.write(content)
                if not content.endswith("\n"):
                    out.write("\n")
                total_bytes += len(content.encode("utf-8"))
            except Exception as e:
                print(f"Warning: Could not read {rel_path}: {e}", file=sys.stderr)

    file_size_mb = os.path.getsize(OUTPUT_FILE) / (1024 * 1024)
    print(f"[build_gemini_context] Successfully wrote {OUTPUT_FILE}")
    print(f"                       Total files: {len(sorted_files)}")
    print(f"                       Total size:  {file_size_mb:.2f} MB")


if __name__ == "__main__":
    build_context()
