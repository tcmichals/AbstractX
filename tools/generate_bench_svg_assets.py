#!/usr/bin/env python3
"""
generate_bench_svg_assets.py - Generates the wake-path diagram of Part 4 (AbstractX alone vs on FreeRTOS
on the XuanTie E906): docs/articles/linux-vs-freertos-vs-abstractx/assets/wake-path-comparison.svg.
The table SVGs it used to write held numbers that were not taken from evidence; the article's tables
are Markdown with evidence links (removed 2026-11-11).
# @impl [SPEC-E906-BENCH-02] [SPEC-E906-RTOS-07] [SPEC-E906-RTOS-08]
"""

import os
import xml.etree.ElementTree as ET

ASSETS_DIR = os.path.abspath("docs/articles/linux-vs-freertos-vs-abstractx/assets")

def escape_xml(s: str) -> str:
    return (s.replace("&", "&amp;")
             .replace("<", "&lt;")
             .replace(">", "&gt;")
             .replace('"', "&quot;"))

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
    lines.append('  <text x="52" y="104" fill="#6fe3ff" class="box-title">AbstractX Alone (no task switch)</text>')

    bm_steps = [
        ("1. Hardware Interrupt (CLIC)", "Line traps to the vector in startup.S; context saved"),
        ("2. ISR Handler (fc_msgbox_doorbell_isr)", "Drains msgbox FIFO, calls IsrDispatcher::post(h)"),
        ("3. Ready queue (SPSC)", "etl::queue_spsc_isr; ISR side unlocked, loop side InterruptLock"),
        ("4. WFI Idle Wake", "mret restores the context; if asleep, continues past wfi"),
        ("5. step() resumes the coroutine", "Dispatcher::process() calls .resume(); same stack, no scheduler")
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
        ("4. Context Switch & Scheduler", "If the task was blocked: scheduler picks it, loads its stack"),
        ("5. Task Unblocks & Pumps Loop", "ulTaskNotifyTake() returns (hosts/freertos) -> step() resumes")
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

def main():
    os.makedirs(ASSETS_DIR, exist_ok=True)
    render_wake_path_svg()


if __name__ == "__main__":
    main()
