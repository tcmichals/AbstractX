#!/usr/bin/env python3
"""
generate_bench_svg_assets.py - Generates clean, dark-themed SVG tables and architecture
diagrams for the AbstractX vs FreeRTOS on XuanTie E906 article.
Outputs to docs/articles/abstractx-vs-freertos-e906/assets/
# @impl [SPEC-E906-BENCH-02] [SPEC-E906-RTOS-07] [SPEC-E906-RTOS-08]
"""

import os
import xml.etree.ElementTree as ET

ASSETS_DIR = os.path.abspath("docs/articles/abstractx-vs-freertos-e906/assets")

def escape_xml(s: str) -> str:
    return (s.replace("&", "&amp;")
             .replace("<", "&lt;")
             .replace(">", "&gt;")
             .replace('"', "&quot;"))

def render_table_svg(
    filename: str,
    tag: str,
    title: str,
    columns: list[dict],
    rows: list[list[str]],
    highlight_row_idx: int = -1,
    col_mono: list[bool] = None
):
    pad_x = 32
    top_y = 75
    header_h = 38
    row_pad_y = 10
    line_h = 18

    total_table_w = sum(c["width"] for c in columns)
    svg_w = total_table_w + pad_x * 2

    if col_mono is None:
        col_mono = [False] * len(columns)

    processed_rows = []
    current_y = top_y + header_h

    for r_idx, row in enumerate(rows):
        row_lines = []
        max_lines_in_row = 1
        for c_idx, cell_text in enumerate(row):
            col = columns[c_idx]
            max_char_width = max(10, int(col["width"] / 7.2) - 3)

            words = str(cell_text).split(" ")
            lines = []
            cur_line = []
            cur_len = 0
            for w in words:
                if cur_len + len(w) + (1 if cur_line else 0) <= max_char_width:
                    cur_line.append(w)
                    cur_len += len(w) + (1 if len(cur_line) > 1 else 0)
                else:
                    if cur_line:
                        lines.append(" ".join(cur_line))
                    cur_line = [w]
                    cur_len = len(w)
            if cur_line:
                lines.append(" ".join(cur_line))

            if not lines:
                lines = [""]
            row_lines.append(lines)
            if len(lines) > max_lines_in_row:
                max_lines_in_row = len(lines)

        row_h = row_pad_y * 2 + max_lines_in_row * line_h
        processed_rows.append({
            "idx": r_idx,
            "y": current_y,
            "h": row_h,
            "cells": row_lines,
            "is_highlight": (r_idx == highlight_row_idx)
        })
        current_y += row_h

    total_table_h = current_y - top_y
    svg_h = current_y + 36

    lines = []
    lines.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {svg_w} {svg_h}" width="{svg_w}" height="{svg_h}" role="img">')
    lines.append('  <defs>')
    lines.append('    <style>')
    lines.append('      text { font-family: Inter, -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }')
    lines.append('      .tag { font-size: 11px; font-weight: 800; letter-spacing: 1.2px; fill: #49d9f4; }')
    lines.append('      .title { font-size: 18px; font-weight: 700; fill: #f4f7ff; }')
    lines.append('      .th { font-size: 11px; font-weight: 800; letter-spacing: 0.8px; fill: #73e5fb; text-transform: uppercase; }')
    lines.append('      .cell { font-size: 13px; fill: #c0cbe0; }')
    lines.append('      .cell-bold { font-size: 13px; font-weight: 700; fill: #f4f7ff; }')
    lines.append('      .cell-mono { font-size: 12px; font-family: ui-monospace, Menlo, Consolas, monospace; fill: #8ee3fb; }')
    lines.append('      .cell-accent { font-size: 13px; font-weight: 700; fill: #8ff0c0; }')
    lines.append('    </style>')
    lines.append('  </defs>')

    # Outer Background
    lines.append(f'  <rect width="{svg_w}" height="{svg_h}" rx="18" fill="#10192c" stroke="#1f2d47" stroke-width="1.5"/>')

    # Header tag and title
    lines.append(f'  <text x="{pad_x}" y="32" class="tag">{escape_xml(tag)}</text>')
    lines.append(f'  <text x="{pad_x}" y="56" class="title">{escape_xml(title)}</text>')

    # Table Container Box
    lines.append(f'  <rect x="{pad_x}" y="{top_y}" width="{total_table_w}" height="{total_table_h}" rx="10" fill="#131d31" stroke="#253754" stroke-width="1.5"/>')

    # Table Header Row Background
    lines.append(f'  <path d="M {pad_x} {top_y + 10} Q {pad_x} {top_y} {pad_x + 10} {top_y} H {pad_x + total_table_w - 10} Q {pad_x + total_table_w} {top_y} {pad_x + total_table_w} {top_y + 10} V {top_y + header_h} H {pad_x} Z" fill="#1c2b46"/>')
    lines.append(f'  <line x1="{pad_x}" y1="{top_y + header_h}" x2="{pad_x + total_table_w}" y2="{top_y + header_h}" stroke="#2d4263" stroke-width="1.5"/>')

    # Column Headers
    col_x = pad_x
    for c_idx, col in enumerate(columns):
        th_align = col.get("align", "left")
        if th_align == "center":
            text_x = col_x + col["width"] / 2
            anchor = ' text-anchor="middle"'
        elif th_align == "right":
            text_x = col_x + col["width"] - 14
            anchor = ' text-anchor="end"'
        else:
            text_x = col_x + 14
            anchor = ''

        lines.append(f'  <text x="{text_x}" y="{top_y + 24}" class="th"{anchor}>{escape_xml(col["name"])}</text>')
        col_x += col["width"]

    # Render Rows
    for r in processed_rows:
        ry = r["y"]
        rh = r["h"]
        is_hl = r["is_highlight"]

        if is_hl:
            lines.append(f'  <rect x="{pad_x}" y="{ry}" width="{total_table_w}" height="{rh}" fill="#163c52" stroke="#42d8f5" stroke-width="1.5"/>')
        else:
            bg_fill = "#141f36" if (r["idx"] % 2 == 0) else "#182540"
            lines.append(f'  <rect x="{pad_x}" y="{ry}" width="{total_table_w}" height="{rh}" fill="{bg_fill}"/>')
            lines.append(f'  <line x1="{pad_x}" y1="{ry + rh}" x2="{pad_x + total_table_w}" y2="{ry + rh}" stroke="#233554" stroke-width="1"/>')

        col_x = pad_x
        for c_idx, lines_in_cell in enumerate(r["cells"]):
            col = columns[c_idx]
            align = col.get("align", "left")
            if align == "center":
                cx = col_x + col["width"] / 2
                anchor = ' text-anchor="middle"'
            elif align == "right":
                cx = col_x + col["width"] - 14
                anchor = ' text-anchor="end"'
            else:
                cx = col_x + 14
                anchor = ''

            if is_hl:
                c_class = "cell-bold"
            elif col_mono[c_idx]:
                c_class = "cell-mono"
            elif c_idx == 0:
                c_class = "cell-bold"
            else:
                c_class = "cell"

            first_line_y = ry + row_pad_y + 13
            for l_idx, line_text in enumerate(lines_in_cell):
                ly = first_line_y + l_idx * line_h
                lines.append(f'  <text x="{cx}" y="{ly}" class="{c_class}"{anchor}>{escape_xml(line_text)}</text>')

            col_x += col["width"]

    lines.append('</svg>\n')
    svg_content = "\n".join(lines)
    ET.fromstring(svg_content)

    out_path = os.path.join(ASSETS_DIR, filename)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(svg_content)
    print(f"Generated {filename} ({svg_w}x{svg_h})")

def render_wake_path_svg():
    svg_w = 980
    svg_h = 520
    lines = []
    lines.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {svg_w} {svg_h}" width="{svg_w}" height="{svg_h}" role="img">')
    lines.append('  <defs>')
    lines.append('    <style>')
    lines.append('      text { font-family: Inter, ui-sans-serif, system-ui, sans-serif; }')
    lines.append('      .tag { font-size: 11px; font-weight: 800; letter-spacing: 1px; fill: #49d9f4; }')
    lines.append('      .title { font-size: 17px; font-weight: 700; fill: #f4f7ff; }')
    lines.append('      .box-title { font-size: 13px; font-weight: 800; }')
    lines.append('      .box-sub { font-size: 11px; fill: #c0cbe0; }')
    lines.append('      .step-num { font-size: 11px; font-weight: 800; fill: #ffd479; }')
    lines.append('    </style>')
    lines.append('  </defs>')
    lines.append(f'  <rect width="{svg_w}" height="{svg_h}" rx="18" fill="#10192c" stroke="#1f2d47" stroke-width="1.5"/>')
    lines.append('  <text x="32" y="32" class="tag">EXECUTION PATH COMPARISON • HARDWARE IRQ TO COROUTINE RESUME</text>')
    lines.append('  <text x="32" y="56" class="title">AbstractX Event Loop vs FreeRTOS Task Notification</text>')

    # Left Column: AbstractX
    lines.append('  <rect x="32" y="75" width="440" height="415" rx="12" fill="#131e33" stroke="#253b5c" stroke-width="1.5"/>')
    lines.append('  <text x="52" y="104" fill="#6fe3ff" class="box-title">AbstractX Alone (Zero Context Switch)</text>')

    bm_steps = [
        ("1. Hardware Interrupt (CLIC)", "Level line triggers CPU trap vector in startup.S (15 cycles)"),
        ("2. ISR Handler (fc_msgbox_doorbell_isr)", "Drains msgbox FIFO, calls IsrDispatcher::post(h)"),
        ("3. Dispatcher SPSC Ring", "Lock-free atomic push into g_isr_ring (no scheduler lock)"),
        ("4. WFI Idle Wake", "CPU returns from ISR past wfi directly in platform_idle_wait()"),
        ("5. abstractx::run() Resumes", "Pumps Dispatcher::process(), calls coroutine .resume() directly")
    ]
    cur_y = 125
    for title, desc in bm_steps:
        lines.append(f'  <rect x="52" y="{cur_y}" width="400" height="50" rx="8" fill="#192845" stroke="#314e7a" stroke-width="1"/>')
        lines.append(f'  <text x="64" y="{cur_y + 20}" fill="#f4f7ff" class="box-title">{escape_xml(title)}</text>')
        lines.append(f'  <text x="64" y="{cur_y + 38}" class="box-sub">{escape_xml(desc)}</text>')
        if cur_y < 340:
            lines.append(f'  <path d="M 252 {cur_y + 50} V {cur_y + 60}" stroke="#49d9f4" stroke-width="2"/>')
            lines.append(f'  <polygon points="252,{cur_y + 64} 248,{cur_y + 58} 256,{cur_y + 58}" fill="#49d9f4"/>')
        cur_y += 62

    # Right Column: FreeRTOS
    lines.append('  <rect x="508" y="75" width="440" height="415" rx="12" fill="#131e33" stroke="#253b5c" stroke-width="1.5"/>')
    lines.append('  <text x="528" y="104" fill="#a78bfa" class="box-title">FreeRTOS + AbstractX (Preemptive Task Notification)</text>')

    rtos_steps = [
        ("1. Hardware Interrupt (CLIC)", "Trap vector enters freertos_risc_v_trap_handler (portASM_e906.S)"),
        ("2. ISR Handler & Post", "Calls fc_msgbox_doorbell_isr() -> IsrDispatcher::post(h)"),
        ("3. Task Notification", "vTaskNotifyGiveFromISR(s_ax_task) + portYIELD_FROM_ISR()"),
        ("4. Context Switch & Scheduler", "Saves task registers, scheduler switches context to abstractx_task"),
        ("5. Task Unblocks & Pumps Loop", "ulTaskNotifyTake() returns -> abstractx::step() resumes coroutine")
    ]
    cur_y = 125
    for title, desc in rtos_steps:
        lines.append(f'  <rect x="528" y="{cur_y}" width="400" height="50" rx="8" fill="#231e3d" stroke="#5b4886" stroke-width="1"/>')
        lines.append(f'  <text x="540" y="{cur_y + 20}" fill="#f4f7ff" class="box-title">{escape_xml(title)}</text>')
        lines.append(f'  <text x="540" y="{cur_y + 38}" class="box-sub">{escape_xml(desc)}</text>')
        if cur_y < 340:
            lines.append(f'  <path d="M 728 {cur_y + 50} V {cur_y + 60}" stroke="#a78bfa" stroke-width="2"/>')
            lines.append(f'  <polygon points="728,{cur_y + 64} 724,{cur_y + 58} 732,{cur_y + 58}" fill="#a78bfa"/>')
        cur_y += 62

    lines.append('</svg>\n')
    svg_content = "\n".join(lines)
    ET.fromstring(svg_content)
    out_path = os.path.join(ASSETS_DIR, "wake-path-comparison.svg")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(svg_content)
    print("Generated wake-path-comparison.svg")

def render_driver_wrapper_svg():
    svg_w = 980
    svg_h = 420
    lines = []
    lines.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {svg_w} {svg_h}" width="{svg_w}" height="{svg_h}" role="img">')
    lines.append('  <defs>')
    lines.append('    <style>')
    lines.append('      text { font-family: Inter, ui-sans-serif, system-ui, sans-serif; }')
    lines.append('      .tag { font-size: 11px; font-weight: 800; letter-spacing: 1px; fill: #49d9f4; }')
    lines.append('      .title { font-size: 17px; font-weight: 700; fill: #f4f7ff; }')
    lines.append('      .box-title { font-size: 13px; font-weight: 800; fill: #f4f7ff; }')
    lines.append('      .box-sub { font-size: 11px; fill: #c0cbe0; }')
    lines.append('      .mono { font-size: 11px; font-family: ui-monospace, Menlo, monospace; fill: #8ee3fb; }')
    lines.append('    </style>')
    lines.append('  </defs>')
    lines.append(f'  <rect width="{svg_w}" height="{svg_h}" rx="18" fill="#10192c" stroke="#1f2d47" stroke-width="1.5"/>')
    lines.append('  <text x="32" y="32" class="tag">ZERO DUPLICATION DRIVER ARCHITECTURE • SPEC-E906-RTOS-08</text>')
    lines.append('  <text x="32" y="56" class="title">Wrapping Freestanding AbstractX HAL Drivers for FreeRTOS Tasks</text>')

    # Box 1: FreeRTOS Task
    lines.append('  <rect x="32" y="90" width="260" height="280" rx="12" fill="#1e1833" stroke="#6b46c1" stroke-width="1.5"/>')
    lines.append('  <text x="48" y="120" fill="#d6bcfa" class="box-title">FreeRTOS Task Domain</text>')
    lines.append('  <text x="48" y="145" class="box-sub">Traditional blocking RTOS code</text>')
    lines.append('  <rect x="48" y="170" width="228" height="60" rx="8" fill="#2d224d"/>')
    lines.append('  <text x="58" y="195" class="mono">uart.read(buf, 100);</text>')
    lines.append('  <text x="58" y="215" class="box-sub">Blocks calling task with timeout</text>')
    lines.append('  <text x="48" y="270" class="box-sub">• Task yields CPU immediately</text>')
    lines.append('  <text x="48" y="295" class="box-sub">• Other tasks or wfi run</text>')
    lines.append('  <text x="48" y="320" class="box-sub">• Zero polling / spinloops</text>')

    # Box 2: FreeRTOS Driver Wrapper Adapter
    lines.append('  <rect x="340" y="90" width="280" height="280" rx="12" fill="#13243d" stroke="#3182ce" stroke-width="1.5"/>')
    lines.append('  <text x="356" y="120" fill="#90cdf4" class="box-title">TaskNotifier Wrapper</text>')
    lines.append('  <text x="356" y="145" class="box-sub">Bridge between RTOS and HAL</text>')
    lines.append('  <rect x="356" y="170" width="248" height="75" rx="8" fill="#1a365d"/>')
    lines.append('  <text x="366" y="193" class="mono">notifier.prepare();</text>')
    lines.append('  <text x="366" y="213" class="mono">hal.start_transfer(req);</text>')
    lines.append('  <text x="366" y="233" class="mono">notifier.wait(timeout);</text>')
    lines.append('  <text x="356" y="280" class="box-sub">• Stores xTaskGetCurrentTaskHandle()</text>')
    lines.append('  <text x="356" y="305" class="box-sub">• Calls ulTaskNotifyTake(pdTRUE)</text>')
    lines.append('  <text x="356" y="330" class="box-sub">• Wakes on vTaskNotifyGiveFromISR</text>')

    # Box 3: AbstractX Core HAL Driver
    lines.append('  <rect x="668" y="90" width="280" height="280" rx="12" fill="#132a2f" stroke="#319795" stroke-width="1.5"/>')
    lines.append('  <text x="684" y="120" fill="#81e6d9" class="box-title">AbstractX Freestanding HAL</text>')
    lines.append('  <text x="684" y="145" class="box-sub">Shared silicon driver (Single SSOT)</text>')
    lines.append('  <rect x="684" y="170" width="248" height="75" rx="8" fill="#1d4044"/>')
    lines.append('  <text x="694" y="193" class="mono">Hardware DMA / FIFO Start</text>')
    lines.append('  <text x="694" y="213" class="mono">CLIC ISR Entry (Line 90)</text>')
    lines.append('  <text x="694" y="233" class="mono">notifier.notify_from_isr()</text>')
    lines.append('  <text x="684" y="280" class="box-sub">• Exact same silicon registers</text>')
    lines.append('  <text x="684" y="305" class="box-sub">• Zero duplicated register logic</text>')
    lines.append('  <text x="684" y="330" class="box-sub">• Shared by Coroutines &amp; RTOS</text>')

    # Arrows
    lines.append('  <path d="M 292 205 H 336" stroke="#63b3ed" stroke-width="2"/>')
    lines.append('  <polygon points="338,205 330,200 330,210" fill="#63b3ed"/>')

    lines.append('  <path d="M 620 205 H 664" stroke="#4fd1c5" stroke-width="2"/>')
    lines.append('  <polygon points="666,205 658,200 658,210" fill="#4fd1c5"/>')

    lines.append('</svg>\n')
    svg_content = "\n".join(lines)
    ET.fromstring(svg_content)
    out_path = os.path.join(ASSETS_DIR, "freertos-driver-wrapper.svg")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(svg_content)
    print("Generated freertos-driver-wrapper.svg")

def main():
    os.makedirs(ASSETS_DIR, exist_ok=True)

    # Table 1: Architecture Comparison
    render_table_svg(
        filename="table1-architecture-comparison.svg",
        tag="RUNTIME ARCHITECTURE • XUANTIE E906 (RV32IMAFC @ 200 MHZ)",
        title="AbstractX Alone vs AbstractX on FreeRTOS",
        columns=[
            {"name": "Architectural Dimension", "width": 220, "align": "left"},
            {"name": "AbstractX alone", "width": 360, "align": "left"},
            {"name": "AbstractX on FreeRTOS", "width": 360, "align": "left"}
        ],
        rows=[
            ["Firmware Target", "apps/e906_bench_app", "apps/e906_freertos_app"],
            ["Concurrency Model", "Pure cooperative C++20 coroutines", "Preemptive priority-based RTOS tasks"],
            ["Who Runs Coroutines", "abstractx::run() main loop; sleeps in wfi when idle", "Dedicated 'abstractx' task; blocks on task notification"],
            ["ISR -> Coroutine Dispatch", "ISR posts to lock-free SPSC ring; wfi wakes immediately", "ISR posts to SPSC ring and calls vTaskNotifyGiveFromISR()"],
            ["Context Switching", "Zero register save/restore overhead (cooperative frame yield)", "Full 32-register context switch on task preemption"],
            ["Timer Overhead", "No periodic tick; wakes on interrupts and the deadlines it armed (plus the 1 kHz profiler sample, which returns to wfi)", "1000 Hz kernel tick timer (CLINT mtime, CLIC line 7)"],
            ["Observability (barectf)", "Direct CTF Stream 4 (Profile) and Stream 5 (Timeline)", "Native CTF Stream 5 spans for task switches & tick interrupts"],
            ["Peripheral Driver Reuse", "Direct freestanding C++20 HAL drivers (SPI, UART, I2C, DMA)", "Identical HAL drivers via FreeRtosDriverWrapper notification"]
        ],
        col_mono=[False, True, True]
    )

    # Table 2: Memory Footprint Breakdown
    render_table_svg(
        filename="table2-memory-footprint.svg",
        tag="MEMORY BUDGET • ALLWINNER T527 ON-CHIP FAST SRAM (192 KB SPACE 0)",
        title="Static Memory & Stack Allocation Breakdown",
        columns=[
            {"name": "Memory Segment", "width": 220, "align": "left"},
            {"name": "AbstractX alone", "width": 240, "align": "left"},
            {"name": "AbstractX on FreeRTOS", "width": 240, "align": "left"},
            {"name": "Delta & Architectural Analysis", "width": 240, "align": "left"}
        ],
        rows=[
            ["Code (.text + .fastcode)", "77,516 Bytes", "82,432 Bytes", "+4,916 Bytes (FreeRTOS kernel + port asm)"],
            ["Static RAM (.data + .bss)", "25,984 Bytes", "37,672 Bytes", "+11,688 Bytes (task/ISR/idle stacks & TCBs)"],
            ["Startup stack (.stack)", "8,192 Bytes", "8,192 Bytes", "Same in both"],
            ["AbstractX task stack", "None (runs on the startup stack)", "8,192 Bytes (s_ax_stack)", "Dedicated task stack"],
            ["ISR Interrupt Stack", "Reuses current stack", "2,048 Bytes (configISR_STACK_SIZE)", "Dedicated hardware trap stack"],
            ["Idle Task Stack", "None (main loop sleeps in wfi)", "1,024 Bytes (minimal stack)", "Static memory for idle loop"],
            ["Heap Allocation", "No heap_*.c; no malloc() calls in our code", "Same (configSUPPORT_STATIC_ALLOCATION)", "newlib _malloc_r/_free_r/_sbrk still linked in both"]
        ],
        col_mono=[False, True, True, False]
    )

    # Table 3: Empirical Hardware Benchmarks (Measured on Radxa Cubie A5E Silicon)
    render_table_svg(
        filename="table3-measured-benchmarks.svg",
        tag="EMPIRICAL VALIDATION • RADXA CUBIE A5E HARDWARE RUNS (200 MHZ)",
        title="Measured on the Cubie A5E: AbstractX vs FreeRTOS (2026-10-31)",
        columns=[
            {"name": "Benchmark Metric", "width": 260, "align": "left"},
            {"name": "AbstractX alone", "width": 340, "align": "left"},
            {"name": "AbstractX on FreeRTOS", "width": 340, "align": "left"}
        ],
        rows=[
            ["Yield round trip (avg cycles / us)", "2,040 cycles (10.20 us)", "2,044 cycles (10.22 us)"],
            ["Soft IRQ -> ISR entry (avg)", "235 cycles (1.18 us)", "253 cycles (1.27 us)"],
            ["Soft IRQ -> coroutine resumed (avg / max)", "2,847 / 3,613 cycles (14.2 / 18.1 us)", "2,998 / 3,454 cycles (15.0 / 17.3 us)"],
            ["RPMsg echo 16 B (med / p99 / max)", "113.5 / 166.9 / 231.1 us", "101.9 / 159.2 / 218.8 us"],
            ["RPMsg echo 256 B (med / p99 / max)", "238.8 / 270.7 / 301.7 us", "241.6 / 279.1 / 296.9 us"],
            ["RPMsg echo 496 B (med / p99 / max)", "327.9 / 341.5 / 370.4 us", "331.9 / 367.1 / 381.6 us"],
            ["RPMsg throughput (496 B)", "2.97 MB/s", "2.92 MB/s"],
            ["Idle wake-ups", "Not instrumented", "6,366 in 3.84 s (tick + all interrupts)"],
            ["Busy share of the bench run", "Not instrumented", "3.25% (whole run, not the tick alone)"],
        ],
        col_mono=[False, True, True]
    )

    # Table 4: Architectural Decision Matrix
    render_table_svg(
        filename="table4-decision-matrix.svg",
        tag="DESIGN GUIDE • RUNTIME SELECTION HEURISTICS",
        title="When to Choose AbstractX or FreeRTOS",
        columns=[
            {"name": "System Characteristic / Use Case", "width": 300, "align": "left"},
            {"name": "Recommended Runtime", "width": 240, "align": "left"},
            {"name": "Architectural Rationale & Tradeoffs", "width": 400, "align": "left"}
        ],
        rows=[
            ["Offload processor beside Linux (E906, FPGA soft core)", "AbstractX alone", "Smallest footprint: no kernel, no task stacks, no tick; SRAM goes to buffers"],
            ["High-rate streaming (8 kHz IMU, sensors)", "AbstractX alone", "Faster ISR and wake path, no context switch, 11.7 KB less static RAM"],
            ["MicroPython REPL & scripts on E906", "AbstractX alone", "MicroPython VM runs cooperatively without preemptive task contention"],
            ["Long-running compute / FFT / Kalman filters", "AbstractX on FreeRTOS", "Background task can be preempted by real-time sensor I/O task"],
            ["Legacy / 3rd-party blocking vendor stacks", "AbstractX on FreeRTOS", "Vendor code can block in traditional APIs without freezing coroutines"],
            ["Ultra low power / battery operation", "AbstractX alone", "No 1 kHz tick; CPU sleeps in wfi until an interrupt or armed deadline"],
            ["Unified Studio Telemetry & Profiling", "Either (same code)", "Both stream cycle-accurate CTF 1.8 spans & stats to Studio"]
        ],
        col_mono=[False, True, False]
    )

    # Render architecture diagrams
    render_wake_path_svg()
    render_driver_wrapper_svg()

if __name__ == "__main__":
    main()
