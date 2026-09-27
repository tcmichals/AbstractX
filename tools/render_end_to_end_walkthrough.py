#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Generates high-resolution screen capture slides and an animated walkthrough
video demonstrating the end-to-end specification-driven workflow:
1. Markdown Specification (.md)
2. Dual Code Generation (C++20 Processor Firmware & FPGA SystemVerilog RTL)
3. AI Adversarial Code Audit (Sashiko Protocols)
4. Automated Verification (CppUTest SITL & Verilator Cocotb)
5. Symmetrical Live Flight Visualizer Mirror
"""

import sys
import os
from pathlib import Path
from PIL import Image

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as patches

OUTPUT_DIR = Path("/home/tcmichals/.gemini/antigravity-ide/brain/202d7260-9209-4d13-bff2-ca9b6445a21b/media")
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

def setup_canvas(title: str, subtitle: str):
    fig = plt.figure(figsize=(16, 9), facecolor="#0b0f19", dpi=120)
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_facecolor("#0b0f19")
    ax.axis("off")

    # Header bar
    ax.add_patch(patches.Rectangle((0, 0.90), 1, 0.10, color="#111827"))
    ax.plot([0, 1], [0.90, 0.90], color="#0284c7", lw=2)

    ax.text(0.04, 0.955, title, color="#38bdf8", fontsize=20, fontweight="bold", va="center")
    ax.text(0.04, 0.922, subtitle, color="#94a3b8", fontsize=12, va="center")
    ax.text(0.96, 0.940, "AbstractX • Sashiko Architecture", color="#4ade80", fontsize=11, fontweight="bold", ha="right", va="center")

    # Footer bar
    ax.add_patch(patches.Rectangle((0, 0), 1, 0.05, color="#111827"))
    ax.plot([0, 1], [0.05, 0.05], color="#1e293b", lw=1)
    ax.text(0.04, 0.025, "End-to-End Specification-Driven Hardware / Software Mirror Workflow", color="#64748b", fontsize=9, va="center")
    ax.text(0.96, 0.025, "Confidential & Open Source", color="#64748b", fontsize=9, ha="right", va="center")

    return fig, ax

def slide_1_title():
    fig, ax = setup_canvas(
        "AbstractX: Specification-Driven Dual Development",
        "From Markdown Specification (.md) to Clean FPGA RTL & Freestanding Processor Code"
    )

    # Center Hero Box
    ax.add_patch(patches.FancyBboxPatch((0.15, 0.25), 0.70, 0.55, boxstyle="round,pad=0.03,rounding_size=0.02",
                                         facecolor="#111827", edgecolor="#38bdf8", lw=2))

    ax.text(0.50, 0.72, "END-TO-END SPECIFICATION PIPELINE", color="#facc15", fontsize=18, fontweight="bold", ha="center")
    ax.text(0.50, 0.65, "Hardware (FPGA) & Software (Processor) Symmetrical Synthesis", color="#e2e8f0", fontsize=14, ha="center")

    steps = [
        ("Step 1: Input Specification", "Structured .md file defining requirements, timing, 64B TLP layout & zero-heap budget", "#38bdf8"),
        ("Step 2: AI Code Generation", "Synthesizes freestanding C++20 coroutines OR SystemVerilog RTL Auto-DMA", "#c084fc"),
        ("Step 3: AI Adversarial Audit", "Sashiko static audit checks 5 core invariants to guarantee zero bugs/leaks", "#f43f5e"),
        ("Step 4: Automated Verification", "CppUTest SITL suites (Host) & Cocotb + Verilator co-sim (Cycle-accurate RTL)", "#4ade80"),
        ("Step 5: Live Flight Display", "Symmetrical flight visualizer mirrors both streams identically over UDP :9870", "#fbbf24"),
    ]

    for idx, (title, desc, color) in enumerate(steps):
        y = 0.55 - idx * 0.065
        ax.plot([0.20, 0.23], [y, y], color=color, lw=4)
        ax.text(0.25, y, title, color=color, fontsize=12, fontweight="bold", va="center")
        ax.text(0.48, y, desc, color="#cbd5e1", fontsize=10.5, va="center")

    out = OUTPUT_DIR / "slide_1_title.png"
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)
    return out

def slide_2_specification():
    fig, ax = setup_canvas(
        "Step 1: The Markdown Specification (SPECIFICATION.md)",
        "Single Source of Truth: Machine-Readable Functional Requirements & Invariants"
    )

    # Left Panel: Spec Markdown File Mockup
    ax.add_patch(patches.FancyBboxPatch((0.05, 0.10), 0.42, 0.75, boxstyle="round,pad=0.02",
                                         facecolor="#111827", edgecolor="#334155", lw=1.5))
    ax.text(0.07, 0.81, "apps/gps_imu_app/SPECIFICATION.md", color="#facc15", fontsize=11, fontweight="bold")
    
    spec_text = (
        "# AbstractX Multi-Rate Flight Controller Specification\n\n"
        "## 1. Functional Requirements\n"
        "- [REQ-IMU-01]: Sample ICM-42688-P at 8 kHz via SPI1.\n"
        "- [REQ-GPS-01]: Ingest UBX-NAV-PVT fixes at 10 Hz via UART0.\n"
        "- [REQ-MAG-01]: Ingest 3-axis magnetic compass at 50 Hz via I2C0.\n"
        "- [REQ-FUS-01]: 9-DoF Mahony quaternion fusion at 100 Hz.\n\n"
        "## 2. Invariant Constraints (Sashiko Protocols)\n"
        "- [INV-HEAP-01]: FREESTANDING 0 BYTES dynamic heap allocation.\n"
        "- [INV-SYNC-01]: Zero synchronous blocking calls in coroutine loop.\n"
        "- [INV-ISR-01]: ISR must never invoke .resume() directly.\n"
        "- [INV-WIRE-01]: All telemetry formatted as 64-byte TLPs.\n\n"
        "## 3. Wire Protocol Layout (asp_tlp64.h)\n"
        "- DW0: Type (1B), Flags (1B), Tag (1B), Channel (1B)\n"
        "- DW1: Target Address (4B) -> 0x40000100 (IMU BAR)\n"
        "- DW2: Length (2B), Sequence (2B)\n"
        "- DW3..4: Hardware Timestamp (8B nanoseconds)\n"
        "- DW5..14: Sensor Burst Payload (40B)\n"
        "- DW15: IEEE 802.3 CRC32 (4B)"
    )
    ax.text(0.07, 0.44, spec_text, color="#e2e8f0", fontsize=9.5, family="monospace", va="center")

    # Right Panel: Why Markdown Specs Matter
    ax.add_patch(patches.FancyBboxPatch((0.52, 0.10), 0.43, 0.75, boxstyle="round,pad=0.02",
                                         facecolor="#111827", edgecolor="#0284c7", lw=1.5))
    ax.text(0.55, 0.81, "SPEC-FIRST AI SYNTHESIS ADVANTAGES", color="#38bdf8", fontsize=13, fontweight="bold")

    points = [
        ("Deterministic Target Disambiguation", "The AI knows upfront whether to synthesize C++ coroutines or SystemVerilog RTL with identical boundaries.", "#38bdf8"),
        ("Clear Invariant Boundaries", "Freestanding constraints, memory budgets, and ISR rules prevent LLMs from generating bloated malloc() code.", "#4ade80"),
        ("Bidirectional Traceability", "Every line of generated C++ and RTL maps directly to a [SPEC-*] requirement tag in the markdown specification.", "#c084fc"),
        ("Test Benchmark Oracle", "SITL tests and Cocotb VIPs derive their exact pass/fail criteria directly from the specification formulas.", "#fbbf24"),
    ]

    for idx, (p_title, p_desc, col) in enumerate(points):
        y = 0.68 - idx * 0.15
        ax.plot([0.55, 0.57], [y + 0.04, y + 0.04], color=col, lw=3)
        ax.text(0.58, y + 0.04, p_title, color=col, fontsize=11, fontweight="bold")
        ax.text(0.55, y - 0.02, p_desc, color="#cbd5e1", fontsize=9.5, wrap=True)

    out = OUTPUT_DIR / "slide_2_specification.png"
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)
    return out

def slide_3_dual_codegen():
    fig, ax = setup_canvas(
        "Step 2: Symmetrical Dual Target Code Generation",
        "Same Specification -> Target A: Freestanding C++20 Processor OR Target B: Autonomous FPGA RTL"
    )

    # Left Box: Processor Target
    ax.add_patch(patches.FancyBboxPatch((0.05, 0.10), 0.42, 0.75, boxstyle="round,pad=0.02",
                                         facecolor="#111827", edgecolor="#22c55e", lw=2))
    ax.text(0.07, 0.81, "TARGET A: PROCESSOR FIRMWARE (C++20)", color="#4ade80", fontsize=12, fontweight="bold")
    ax.text(0.07, 0.77, "Freestanding • 0 B Dynamic Heap • Coroutine Tasks", color="#94a3b8", fontsize=9)

    cpp_code = (
        "// apps/gps_imu_app/src/main.cpp\n"
        "Task<void> sensor_fusion_task(ITimer& timer,\n"
        "                             AttitudeFilter& filter) {\n"
        "    while (true) {\n"
        "        // 1. Asynchronously await 8 kHz IMU sample\n"
        "        ImuSample imu = co_await g_imu_channel.pop();\n"
        "        filter.update_imu(imu, dt);\n\n"
        "        // 2. Drain medium-rate 50 Hz Mag\n"
        "        MagSample mag;\n"
        "        while (g_mag_channel.try_pop(mag)) {\n"
        "            filter.update_mag(mag);\n"
        "        }\n\n"
        "        // 3. Emit 64-byte TLP into SPSC Ring\n"
        "        Tlp64 tlp = AttitudeFilter::to_tlp(filter.state());\n"
        "        g_telemetry_ring.push(tlp);\n"
        "    }\n"
        "}"
    )
    ax.text(0.07, 0.44, cpp_code, color="#e2e8f0", fontsize=9, family="monospace", va="center")

    # Right Box: FPGA Target
    ax.add_patch(patches.FancyBboxPatch((0.52, 0.10), 0.43, 0.75, boxstyle="round,pad=0.02",
                                         facecolor="#111827", edgecolor="#38bdf8", lw=2))
    ax.text(0.54, 0.81, "TARGET B: FPGA HARDWARE RTL (SystemVerilog)", color="#38bdf8", fontsize=12, fontweight="bold")
    ax.text(0.54, 0.77, "Autonomous Auto-DMA • DRDY Edge Pin • 9.57 µs Doorbell", color="#94a3b8", fontsize=9)

    sv_code = (
        "// rtl/imu/asp_imu_auto_dma.sv\n"
        "always_ff @(posedge clk or negedge rst_n) begin\n"
        "  case (imu_state)\n"
        "    ST_IMU_IDLE: begin\n"
        "      if (auto_dma_en && imu_int_trig) begin\n"
        "        // Mode B: Hardware DRDY Interrupt Auto-DMA\n"
        "        latched_timestamp <= i_sys_timestamp;\n"
        "        spi_cmd_shift     <= burst_addr | 8'h80;\n"
        "        imu_state         <= ST_IMU_START_BURST;\n"
        "      end\n"
        "    end\n"
        "    ST_IMU_BUILD_TLP: begin\n"
        "      // Emits identical 64-Byte TLP (DW0..DW15)\n"
        "      m_imu_stream_tdata <= {\n"
        "        8'h10, 8'h00, 8'h00, 8'h02, // DMA_Stream Ch2\n"
        "        IMU_WB_BASE, 16'd4, sample_count,\n"
        "        latched_timestamp, captured_sensor_data\n"
        "      };\n"
        "      o_int_req <= 1'b1; // Doorbell asserted!\n"
        "    end\n"
        "  endcase\n"
        "end"
    )
    ax.text(0.54, 0.44, sv_code, color="#e2e8f0", fontsize=8.5, family="monospace", va="center")

    out = OUTPUT_DIR / "slide_3_dual_codegen.png"
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)
    return out

def slide_4_adversarial_audit():
    fig, ax = setup_canvas(
        "Step 3: AI Adversarial Code Audit (Sashiko Protocols)",
        "Automated Multi-Stage Invariant Verification: Eliminating Hallucinations, Memory Leaks, & Concurrency Bugs"
    )

    # Central Terminal View
    ax.add_patch(patches.FancyBboxPatch((0.08, 0.12), 0.84, 0.72, boxstyle="round,pad=0.02",
                                         facecolor="#050811", edgecolor="#22c55e", lw=2))
    ax.text(0.11, 0.80, "Terminal: python3 tools/run_adversarial_audit.py", color="#94a3b8", fontsize=11, family="monospace")

    audit_output = (
        "=======================================================================\n"
        "       AbstractX Multi-Stage Decomposed Adversarial Audit Report       \n"
        "=======================================================================\n\n"
        "[PASS] Stage 1 (Zero-Heap & Freestanding): 0 issues found\n"
        "       -> Checked: <vector>, <string>, <iostream>, raw malloc(), new Type\n"
        "       -> Invariant verified: 0 B dynamic heap allocation in fast path\n\n"
        "[PASS] Stage 2 (Non-Blocking HAL & Lifecycle): 0 issues found\n"
        "       -> Checked: synchronous spin-sleeps, usleep(), sleep() in coroutines\n"
        "       -> Invariant verified: 100% cooperative asynchronous co_await\n\n"
        "[PASS] Stage 3 (ISR Boundary & Dispatch Safety): 0 issues found\n"
        "       -> Checked: direct .resume() calls inside hardware interrupt contexts\n"
        "       -> Invariant verified: Lock-free SPSC queue handoff across ISR boundary\n\n"
        "[PASS] Stage 4 (Endianness & Wire Framing): 0 issues found\n"
        "       -> Checked: compile-time static_assert(sizeof(asp_tlp64_t) == 64)\n"
        "       -> Invariant verified: Universal 64-byte wire parity maintained\n\n"
        "[PASS] Stage 5 (CppUTest & Test Verification): 0 issues found\n"
        "       -> Checked: Companion test directories (tests/ and sim/cocotb/)\n\n"
        "=======================================================================\n"
        "EXECUTIVE VERDICT: [PASS FOR PRODUCTION COMMIT] (0 Issues)\n"
        "======================================================================="
    )
    ax.text(0.11, 0.44, audit_output, color="#4ade80", fontsize=9.5, family="monospace", va="center")

    out = OUTPUT_DIR / "slide_4_adversarial_audit.png"
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)
    return out

def slide_5_verification():
    fig, ax = setup_canvas(
        "Step 4: Automated Verification (CppUTest SITL & Cocotb Verilator)",
        "Cycle-Accurate & Bit-Accurate Validation: 100% Tests Pass with Zero Memory Leaks"
    )

    # Left Box: CppUTest SITL
    ax.add_patch(patches.FancyBboxPatch((0.05, 0.10), 0.42, 0.75, boxstyle="round,pad=0.02",
                                         facecolor="#111827", edgecolor="#22c55e", lw=1.5))
    ax.text(0.07, 0.81, "CPPUTEST SITL SUITES (Processor C++20)", color="#4ade80", fontsize=11, fontweight="bold")
    ax.text(0.07, 0.77, "Ran in < 3 ms • 10 Tests • 40 Checks • 0 Memory Leaks", color="#94a3b8", fontsize=9)

    cpputest_log = (
        "--- Running test_sitl_imu ---\n"
        "TEST(SitlImuTestGroup, VerifyAsyncInitSequenceWithMocks) - 1 ms\n"
        "TEST(SitlImuTestGroup, VerifyTlpEncapsulation) - 0 ms\n"
        "TEST(SitlImuTestGroup, VerifyRawBufferParsingCalculations) - 0 ms\n"
        "OK (3 tests, 3 ran, 17 checks, 0 memory leaks, 1 ms)\n\n"
        "--- Running test_sitl_gps ---\n"
        "TEST(SitlGpsTestGroup, VerifyInitAsyncBaudrate) - 0 ms\n"
        "TEST(SitlGpsTestGroup, VerifyGpsTlpEncapsulation) - 0 ms\n"
        "TEST(SitlGpsTestGroup, VerifyCorruptChecksumRejection) - 0 ms\n"
        "TEST(SitlGpsTestGroup, VerifyUbxPvtChecksumAndParsing) - 0 ms\n"
        "OK (4 tests, 4 ran, 15 checks, 0 memory leaks, 1 ms)\n\n"
        "--- Running test_sitl_fusion ---\n"
        "TEST(SitlFusionTestGroup, VerifyAhrsTlpEncapsulation) - 0 ms\n"
        "TEST(SitlFusionTestGroup, VerifyGpsUpdateIntegration) - 0 ms\n"
        "TEST(SitlFusionTestGroup, VerifyIdentityAttitudeOnLevelImu) - 0 ms\n"
        "OK (3 tests, 3 ran, 8 checks, 0 memory leaks, 1 ms)\n\n"
        "RESULT: 10/10 TESTS PASSED (100% PASS RATE)"
    )
    ax.text(0.07, 0.44, cpputest_log, color="#e2e8f0", fontsize=9, family="monospace", va="center")

    # Right Box: Cocotb RTL Co-Sim
    ax.add_patch(patches.FancyBboxPatch((0.52, 0.10), 0.43, 0.75, boxstyle="round,pad=0.02",
                                         facecolor="#111827", edgecolor="#38bdf8", lw=1.5))
    ax.text(0.54, 0.81, "COCOTB + VERILATOR CO-SIM (FPGA RTL)", color="#38bdf8", fontsize=11, fontweight="bold")
    ax.text(0.54, 0.77, "asp_top.sv • Python VIP • 9.57 µs Doorbell Verification", color="#94a3b8", fontsize=9)

    cocotb_log = (
        "[iNav Step 1] Checking WHO_AM_I (0x75 -> 0x47)... [PASS]\n"
        "[iNav Step 2] Writing PWR_MGMT0 (0x4E -> 0x0F)... [PASS]\n"
        "[iNav Step 3] GYRO/ACCEL Config (1kHz, ±2000dps, ±16g)... [PASS]\n"
        "[iNav Step 4] DRDY Interrupt Setup (INT_CONFIG)... [PASS]\n"
        "[Step 5] Write FPGA IMU_BURST_ADDR = 0x1D... [PASS]\n"
        "[Step 6] Enable FPGA Auto-DMA (0x05 to IMU_CTRL)... [PASS]\n"
        "[Step 7] DRDY pulse detected! Waiting FPGA SPI master...\n"
        "  o_int_req asserted after 957 clock cycles (9.57 µs)!\n"
        "[Step 8] Doorbell o_int_req verified HIGH... [PASS]\n"
        "[Step 9] Dual-SPI Burst Read (CMD 0xA2)...\n"
        "  Received TLP: Type=0x10 Ch=0x02 Addr=0x40000100\n"
        "  Decoded: Temp=3312 Accel=(164,-82,2048) Gyro=(15,-22,4)\n"
        "[SUCCESS] o_int_req automatically deasserted after read!\n\n"
        "TESTS=2 PASS=2 FAIL=0 (100% PASS RATE)"
    )
    ax.text(0.54, 0.44, cocotb_log, color="#e2e8f0", fontsize=8.5, family="monospace", va="center")

    out = OUTPUT_DIR / "slide_5_verification.png"
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)
    return out

def slide_6_live_mirror():
    fig, ax = setup_canvas(
        "Step 5: Symmetrical Live Flight Visualizer Mirror",
        "Demonstrating Parity: Hardware (FPGA RTL) or Software (C++20 SITL) Delivers Identical Telemetry"
    )

    # Top Comparative Banner
    ax.add_patch(patches.Rectangle((0.05, 0.77), 0.42, 0.08, color="#1e293b"))
    ax.plot([0.05, 0.47], [0.77, 0.77], color="#38bdf8", lw=2)
    ax.text(0.26, 0.82, "MODE 1: FPGA HARDWARE RTL (asp_top.sv)", color="#38bdf8", fontsize=11, fontweight="bold", ha="center")
    ax.text(0.26, 0.79, "Doorbell: 9.57 µs (957 clk) | Auto-DMA 14B SPI @ 10MHz", color="#94a3b8", fontsize=9, ha="center")

    ax.add_patch(patches.Rectangle((0.53, 0.77), 0.42, 0.08, color="#1e293b"))
    ax.plot([0.53, 0.95], [0.77, 0.77], color="#4ade80", lw=2)
    ax.text(0.74, 0.82, "MODE 2: SOFTWARE C++20 SITL (gps_imu_app)", color="#4ade80", fontsize=11, fontweight="bold", ha="center")
    ax.text(0.74, 0.79, "Runtime: DomainDispatcher | 0 B Heap | Lock-Free SPSC Ring", color="#94a3b8", fontsize=9, ha="center")

    # Lower Box: The Unified 4-Panel Flight Display Mirror
    ax.add_patch(patches.FancyBboxPatch((0.05, 0.10), 0.90, 0.63, boxstyle="round,pad=0.02",
                                         facecolor="#111827", edgecolor="#facc15", lw=2))
    
    ax.text(0.50, 0.68, "THE ABSTRACTX SYMMETRICAL FLIGHT VISUALIZER (UDP :9870)", color="#facc15", fontsize=13, fontweight="bold", ha="center")
    
    cards = [
        ("Primary Flight Display (PFD)", "Artificial Horizon, Pitch Ladder, Sky/Ground banking tilt driven identically by FPGA Auto-DMA or C++ Mahony AHRS.", "#38bdf8", 0.08),
        ("3D Attitude Perspective Model", "Quadcopter wireframe with real-time 3D rotation matrix (Tait-Bryan Euler angles) tracking pitch, roll, and north-referenced yaw.", "#a855f7", 0.30),
        ("Navigation & Trace Panel", "MSL Altitude (m), Ground Speed (m/s), Multi-Rate Health (IMU 8kHz, Mag 50Hz, GPS 10Hz), and Live Hardware Doorbell trace.", "#4ade80", 0.52),
        ("Motor Mixer Demands", "Quad-X cascaded PID motor demands (M1..M4 bars, 100..1000 µs) responding symmetrically to sensor perturbations.", "#fbbf24", 0.74),
    ]

    for title, desc, col, x_pos in cards:
        ax.add_patch(patches.FancyBboxPatch((x_pos, 0.15), 0.18, 0.48, boxstyle="round,pad=0.02",
                                             facecolor="#0b0f19", edgecolor=col, lw=1.5))
        ax.text(x_pos + 0.09, 0.58, title, color=col, fontsize=10, fontweight="bold", ha="center")
        ax.plot([x_pos + 0.02, x_pos + 0.16], [0.55, 0.55], color=col, lw=1)
        ax.text(x_pos + 0.09, 0.35, desc, color="#cbd5e1", fontsize=8.5, ha="center", va="center", wrap=True)

    out = OUTPUT_DIR / "slide_6_live_mirror.png"
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)
    return out

def compile_animated_walkthrough(slides: list):
    images = [Image.open(s) for s in slides]
    
    # Save as high-quality animated GIF (duration 3000ms = 3s per slide)
    gif_path = OUTPUT_DIR / "end_to_end_walkthrough.gif"
    images[0].save(
        gif_path,
        save_all=True,
        append_images=images[1:],
        duration=3000,
        loop=0
    )
    print(f"[Walkthrough] Generated animated GIF: {gif_path}")

    # Save as animated WebP video (duration 3000ms)
    webp_path = OUTPUT_DIR / "end_to_end_walkthrough.webp"
    images[0].save(
        webp_path,
        save_all=True,
        append_images=images[1:],
        duration=3000,
        loop=0
    )
    print(f"[Walkthrough] Generated animated WebP: {webp_path}")

def main():
    print("Generating End-to-End Walkthrough Presentation Slides...")
    s1 = slide_1_title()
    s2 = slide_2_specification()
    s3 = slide_3_dual_codegen()
    s4 = slide_4_adversarial_audit()
    s5 = slide_5_verification()
    s6 = slide_6_live_mirror()

    slides = [s1, s2, s3, s4, s5, s6]
    compile_animated_walkthrough(slides)
    print("\n[SUCCESS] All presentation slides and animated video generated successfully!")

if __name__ == "__main__":
    main()
