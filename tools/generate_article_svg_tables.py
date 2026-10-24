#!/usr/bin/env python3
"""
Generates clean, dark-themed SVG tables for the MicroPython on E906 Hackster article.
Outputs to docs/articles/micropython-on-e906/assets/
# @impl [SPEC-E906-REPL-20] apps/e906_repl_app/SPECIFICATION.md
"""

import os
import xml.etree.ElementTree as ET

ASSETS_DIR = os.path.abspath("docs/articles/micropython-on-e906/assets")

def escape_xml(s):
    return (s.replace("&", "&amp;")
             .replace("<", "&lt;")
             .replace(">", "&gt;")
             .replace('"', "&quot;"))

def render_table_svg(
    filename: str,
    tag: str,
    title: str,
    columns: list[dict], # [{"name": "Col", "width": 100, "align": "left"|"center"|"right"}]
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

    # Calculate row heights by wrapping text
    # Character wrap estimation: ~8px per char for regular, ~7.5px for mono
    processed_rows = []
    current_y = top_y + header_h

    for r_idx, row in enumerate(rows):
        row_lines = []
        max_lines_in_row = 1
        for c_idx, cell_text in enumerate(row):
            col = columns[c_idx]
            max_char_width = max(10, int(col["width"] / 7.2) - 3)
            
            # Simple word-based wrapping
            words = cell_text.split(" ")
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

    # Build SVG XML
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

        # Row background
        if is_hl:
            lines.append(f'  <rect x="{pad_x}" y="{ry}" width="{total_table_w}" height="{rh}" fill="#163c52" stroke="#42d8f5" stroke-width="1.5"/>')
        else:
            bg_fill = "#141f36" if (r["idx"] % 2 == 0) else "#182540"
            lines.append(f'  <rect x="{pad_x}" y="{ry}" width="{total_table_w}" height="{rh}" fill="{bg_fill}"/>')
            lines.append(f'  <line x1="{pad_x}" y1="{ry + rh}" x2="{pad_x + total_table_w}" y2="{ry + rh}" stroke="#233554" stroke-width="1"/>')

        # Cell contents
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
            
            # Pick class
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

    # Validate XML
    ET.fromstring(svg_content)

    out_path = os.path.join(ASSETS_DIR, filename)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(svg_content)
    print(f"Generated {filename} ({svg_w}x{svg_h})")

def main():
    os.makedirs(ASSETS_DIR, exist_ok=True)

    # Table 1: Series Roadmap
    render_table_svg(
        filename="table1-series-roadmap.svg",
        tag="SERIES ROADMAP • UNLOCKING THE XUANTIE E906",
        title="Allwinner T527 RISC-V Firmware & MicroPython Series",
        columns=[
            {"name": "Part", "width": 80, "align": "center"},
            {"name": "Topic & Architectural Focus", "width": 640, "align": "left"},
            {"name": "Platform", "width": 200, "align": "center"}
        ],
        rows=[
            ["1", "The T527's hidden RISC-V core: why it is useful, memory windows, SRAM booting, JTAG debugging", "Hackster (Part 1)"],
            ["2", "Linux remoteproc driver that loads and starts the E906, plus hardware test suite", "Hackster (Part 2)"],
            ["3", "Linux ↔ RISC-V messaging: mailbox doorbell, shared SRAM rings and VirtIO RPMsg", "GitHub (Part 3)"],
            ["4", "C++20 coroutines vs FreeRTOS on the bare-metal E906", "GitHub (Part 4)"],
            ["Side", "Device-tree overlays in U-Boot: how config.txt configures pinmux at boot", "GitHub (Devicetree)"],
            ["5", "This article: MicroPython on both processors, RPMsg streams, live profiling, DMA", "Hackster (Current)"]
        ],
        highlight_row_idx=5,
        col_mono=[True, False, False]
    )

    # Table 2: SoC Hardware Architecture Blocks
    render_table_svg(
        filename="table2-architecture-blocks.svg",
        tag="SOC ARCHITECTURE • RADXA CUBIE A5E (ALLWINNER T527)",
        title="Hardware Execution Domains & Peripheral Partitioning",
        columns=[
            {"name": "Hardware Block", "width": 220, "align": "left"},
            {"name": "Silicon Specifications & Features", "width": 460, "align": "left"},
            {"name": "Assigned Role in System", "width": 240, "align": "left"}
        ],
        rows=[
            ["8 × ARM Cortex-A55", "Application cores @ 1.4 GHz, 64-bit ARMv8.2, Linux OS", "Linux userland, Python bridges, PC SSH/UDP"],
            ["XuanTie E906", "32-bit RISC-V (RV32IMAFC) @ 200 MHz, FPU, CLIC, no MMU", "AbstractX bare-metal firmware & MicroPython REPL"],
            ["On-Chip SRAM", "Two 256 KB SRAM windows (Space 0 @ 0x3FFC0000, Space 1 @ 0x40000000)", "Zero-wait firmware, vectors, VirtIO RPMsg buffers"],
            ["Message Box", "Hardware dual-core mailbox with low-latency IRQ generation", "Bidirectional 'look-now' doorbell for RPMsg"],
            ["UART, I²C, SPI, DMA", "Hardware peripherals on 40-pin header + multi-channel DMA", "Dedicated real-time I/O isolated from Linux kernel"]
        ],
        col_mono=[False, False, False]
    )

    # Table 3: VirtIO RPMsg Endpoints
    render_table_svg(
        filename="table3-rpmsg-endpoints.svg",
        tag="INTER-PROCESSOR COMMUNICATION • VIRTIO RPMSG",
        title="Shared SRAM Virtual Endpoints & Wire Protocols",
        columns=[
            {"name": "Endpoint", "width": 100, "align": "center"},
            {"name": "Service Name", "width": 180, "align": "left"},
            {"name": "Payload & Format", "width": 280, "align": "left"},
            {"name": "Buffering & Transmission Strategy", "width": 360, "align": "left"}
        ],
        rows=[
            ["1026", "abstractx-repl", "MicroPython text: REPL prompts, raw script input, stdout", "Stream segmented into 256-byte chunks; reassembled in-place on E906"],
            ["1024", "abstractx-tlp", "Fixed 64-byte binary frames (sensors, attitude, commands)", "Batched: up to 7 TLP frames packed into one 496-byte RPMsg buffer"],
            ["1025", "abstractx-trace", "CTF 1.8 / barectf binary traces, profiler samples, spans", "Zero-copy: trace events written directly into RPMsg TX buffer without copy"]
        ],
        col_mono=[True, True, False, False]
    )

    # Table 4: modabstractx MicroPython API
    render_table_svg(
        filename="table4-modabstractx-api.svg",
        tag="MICROPYTHON INTEGRATION • MODABSTRACTX BUILT-IN EXTENSION",
        title="Hardware Peripheral & Telemetry C-Binding Functions",
        columns=[
            {"name": "Function & Signature", "width": 380, "align": "left"},
            {"name": "Hardware Action & Behavioral Semantics", "width": 540, "align": "left"}
        ],
        rows=[
            ["i2c_init([hz]), i2c_scan()", "Configures I²C1 controller; scans 7-bit bus addresses non-blockingly"],
            ["i2c_read(addr, reg, n), i2c_write(addr, reg, data)", "Transfers register data to QMC5883L compass; returns empty/0 on NACK or 20 ms timeout"],
            ["uart_init([baud]), uart_read([n]), uart_write(data)", "UART3 (GPS). Reads bytes pre-buffered by interrupt handler; non-blocking"],
            ["uart_status()", "Returns FIFO fill levels and hardware overrun/framing error flags"],
            ["sleep_ms(ms), ticks_ms(), cycles()", "Enters low-power RISC-V wfi sleep; returns millisecond and 200 MHz cycle counters"],
            ["peek(addr), peek_hex(addr), poke(addr, val)", "Direct MMIO: reads or writes any 32-bit hardware register"],
            ["irq_stats()", "Queries CLIC interrupt firing counters and per-driver latency stats"],
            ["trace(task_id, state[, msg])", "Emits zero-copy barectf CTF 1.8 binary trace record to RPMsg endpoint 1025"],
            ["tlp_send(buf), console_read([n])", "Sends 64-byte binary telemetry frame or drains raw REPL input buffer"],
            ["dma_bench(n[, port])", "Runs hardware DMA engine vs CPU memcpy comparison across n bytes"]
        ],
        col_mono=[True, False]
    )

    # Table 5: Concurrency Models Comparison
    render_table_svg(
        filename="table5-async-concurrency.svg",
        tag="EXECUTION MODELS • CONCURRENCY ARCHITECTURE COMPARISON",
        title="Desktop Python vs MicroPython vs AbstractX Coroutines",
        columns=[
            {"name": "Architectural Dimension", "width": 180, "align": "left"},
            {"name": "CPython asyncio (Desktop)", "width": 240, "align": "left"},
            {"name": "MicroPython uasyncio", "width": 240, "align": "left"},
            {"name": "AbstractX Coroutines (C++20)", "width": 260, "align": "left"}
        ],
        rows=[
            ["Primary Domain", "Web APIs, network I/O, databases", "Emulated desktop async on MCUs", "Hardware DMA, SPI, I²C, control loops"],
            ["Steady-State Heap", "Hundreds of KB dynamic allocations", "Allocates Task & closures each run", "0 bytes heap (pre-allocated static pools)"],
            ["Garbage Collector", "Generational GC (negligible in web)", "5–15 ms GC pauses freeze CPU", "0 GC sweeps during steady-state loops"],
            ["Idle Power State", "Epoll / kqueue socket sleep", "Software task-list polling loop", "Hardware RISC-V wfi sleep (0.01% CPU)"],
            ["Path to C++ Flight", "Complete architectural rewrite", "Complete architectural rewrite", "1-to-1 syntactic translation to C++20"]
        ],
        col_mono=[False, False, False, False]
    )

    # Table 6: E906 Memory Layout
    render_table_svg(
        filename="table6-memory-regions.svg",
        tag="MEMORY MAP • ALLWINNER T527 ON-CHIP SRAM ALLOCATION",
        title="SRAM Space Partitioning for RISC-V Firmware & MicroPython",
        columns=[
            {"name": "Memory Segment", "width": 240, "align": "left"},
            {"name": "E906 Address", "width": 160, "align": "center"},
            {"name": "Assigned Data Structures & Firmware Components", "width": 520, "align": "left"}
        ],
        rows=[
            ["SRAM Space 0 (192 KB)", "0x3FFC0000", "Fast vector table, CLIC handlers, Dispatcher, drivers, RPMsg, MicroPython VM, stack"],
            ["SRAM Space 1 (Lower 128 KB)", "0x40000000", "VirtIO resource table, boot log, parser/compiler, profiler & timeline CTF rings"],
            ["SRAM Space 1 (Upper 128 KB)", "0x40020000", "Linux VirtIO RPMsg vrings and shared buffer pool (firmware zero allocation)"],
            ["HiFi4 DSP RAM (128 KB)", "0x00020000", "Dedicated MicroPython GC heap (completely isolated from firmware SRAM)"]
        ],
        col_mono=[False, True, False]
    )

    # Table 7: DMA vs CPU Benchmark
    render_table_svg(
        filename="table7-dma-benchmark.svg",
        tag="HARDWARE BENCHMARK • ON-CHIP DATA TRANSFERS",
        title="Hardware DMA Engine vs XuanTie E906 CPU memcpy",
        columns=[
            {"name": "Payload Size", "width": 160, "align": "center"},
            {"name": "Hardware DMA (Start to Done)", "width": 380, "align": "center"},
            {"name": "CPU memcpy @ 200 MHz", "width": 380, "align": "center"}
        ],
        rows=[
            ["4 Bytes", "4.4 µs (descriptor setup overhead dominates)", "0.45 µs (CPU copy is faster for tiny payloads)"],
            ["64 Bytes", "4.8 µs", "3.9 µs (break-even crossover region)"],
            ["256 Bytes", "7.0 µs (DMA 2.0× faster than CPU)", "14.4 µs"],
            ["1024 Bytes", "10.8 µs (DMA 5.2× faster, bus saturated)", "56.6 µs"]
        ],
        col_mono=[True, False, False]
    )

    # Table 8: Measured Results Summary
    render_table_svg(
        filename="table8-measured-results.svg",
        tag="EMPIRICAL VALIDATION • SYSTEM MEASUREMENTS",
        title="Silicon Timings, Interrupt Latency & Resource Utilization",
        columns=[
            {"name": "Subsystem / Operating Condition", "width": 460, "align": "left"},
            {"name": "Measured Hardware Metric / Latency", "width": 460, "align": "left"}
        ],
        rows=[
            ["Idle board baseline", "0.01% busy (wfi sleep); 0.3–0.4% with 1 kHz profiler active"],
            ["MicroPython sensor streamer (100 ms period)", "repl_rx task consumes ~7.6% CPU"],
            ["MicroPython tight compass + GPS loop", "repl_rx consumes ~90% CPU"],
            ["I²C (compass) interrupt handler", "1.40–1.43 µs per ISR; 11 interrupts per 6-byte register read"],
            ["UART3 (GPS) interrupt (115,200 baud)", "50 µs (polling status/byte) → 31.8 µs (FIFO burst read)"],
            ["Single UART register read latency", "~0.29 µs (58 clock cycles @ 200 MHz)"],
            ["Studio CLI round-trip latency", "~55 ms (SSH + bridge + RPMsg + Python eval + return)"],
            ["Hardware DMA transfer complete ISR", "1.33 µs interrupt execution latency"],
            ["Hardware board self-test suite", "32 passed, 0 failed, 2 skipped (100% pass rate)"]
        ],
        col_mono=[False, False]
    )

if __name__ == "__main__":
    main()
