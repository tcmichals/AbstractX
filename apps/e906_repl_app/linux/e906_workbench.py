#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-E906-REPL-05] [SPEC-E906-IPC-04] apps/e906_repl_app/SPECIFICATION.md
"""
e906_workbench.py - Unified Linux Host Tool for AbstractX XuanTie E906 (TUI & Headless)
----------------------------------------------------------------------------------------
Single unified tool for interacting with the XuanTie E906 coprocessor:
  1. Headless CLI execution (scripts, inline commands, MMIO peek/poke):
       python3 e906_workbench.py -c "import abstractx; print(abstractx.alive())"
       python3 e906_workbench.py --peek 0x02500800  # UART2 GPS
       python3 e906_workbench.py --peek 0x02502400  # TWI1 I2C
       python3 e906_workbench.py script.py

  2. Interactive Full-Screen Terminal Studio (when invoked without commands):
       python3 e906_workbench.py
       - Top Bar: RemoteProc liveness, BareCTF trace packet counter.
       - Tabs: [1] MicroPython REPL, [2] BareCTF/Trace Stream, [3] Hardware Monitor.
       - Pop-up Modals: [F1/?] Help, [F2/G] GPS, [F3/C] Compass, [F4/M] SRAM Map.
"""

import sys
import os
import time
import glob
import struct
import fcntl
import select
import argparse
import subprocess
import threading
from typing import List, Optional

# RemoteProc & RPMsg constants
TRACE0_PATH = "/sys/kernel/debug/remoteproc/remoteproc0/trace0"
RPROC_STATE_PATH = "/sys/class/remoteproc/remoteproc0/state"
RPMSG_CTRL = "/dev/rpmsg_ctrl0"
EPT_INFO = struct.Struct("<32sII")
RPMSG_CREATE_EPT_IOCTL = (1 << 30) | (EPT_INFO.size << 16) | (0xB5 << 8) | 0x1


def exec_remote_cmd(cmd: str, target_ssh: Optional[str] = "root@192.168.3.3") -> str:
    if target_ssh:
        try:
            res = subprocess.run(
                ["ssh", "-q", "-o", "ConnectTimeout=3", target_ssh, cmd],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=4.0,
            )
            return res.stdout
        except Exception:
            return ""
    else:
        try:
            res = subprocess.run(
                cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=4.0
            )
            return res.stdout
        except Exception:
            return ""


def send_code_to_e906(code: str, target_ssh: Optional[str] = "root@192.168.3.3") -> str:
    escaped = code.replace("'", "'\"'\"'")
    script = f"""python3 -c '
import os, glob, select, time
devs = glob.glob("/dev/rpmsg[0-9]*")
if not devs:
    import struct, fcntl
    info = struct.pack("<32sII", b"abstractx-repl", 1026, 1026)
    fd = os.open("/dev/rpmsg_ctrl0", os.O_RDWR)
    try:
        fcntl.ioctl(fd, (1 << 30) | (40 << 16) | (0xB5 << 8) | 0x1, info)
    finally:
        os.close(fd)
    time.sleep(0.05)
    devs = glob.glob("/dev/rpmsg[0-9]*")

if devs:
    try:
        fd = os.open(devs[0], os.O_RDWR | os.O_NONBLOCK)
        os.write(fd, b"{escaped}\\r\\n")
        time.sleep(0.15)
        r, _, _ = select.select([fd], [], [], 0.6)
        if r:
            print(os.read(fd, 2048).decode(errors="replace"), end="")
        else:
            print("[E906 Sent]")
        os.close(fd)
    except Exception as e:
        print("[RPMsg Error:", e, "]")
else:
    print("[No RPMsg endpoint available]")
'"""
    return exec_remote_cmd(script, target_ssh)


class E906WorkbenchTUI:
    def __init__(self, target_ssh: Optional[str] = None):
        self.target_ssh = target_ssh
        self.running = True
        self.active_tab = 1
        self.active_modal: Optional[str] = None
        self.rproc_state = "UNKNOWN"
        self.barectf_packet_count = 0
        self.trace_logs: List[str] = []
        self.repl_output_lines: List[str] = [
            "AbstractX XuanTie E906 MicroPython REPL (Pure SRAM, 0 DDR).",
            "Type MicroPython code and press Enter to execute on E906 coprocessor.",
            "Try: import abstractx; print('Alive:', abstractx.alive())",
        ]
        self.repl_input_buf = ""
        self.gps_info = {"status": "Awaiting Lock", "sats": 0, "fix": "None", "lat": "--", "lon": "--", "alt": "--"}
        self.compass_info = {"status": "Ready", "heading": 0.0, "mag_x": 0, "mag_y": 0, "mag_z": 0, "addr": "0x1E / 0x0D"}
        self.poll_thread = threading.Thread(target=self._background_poller, daemon=True)

    def _background_poller(self):
        last_trace = ""
        while self.running:
            state = exec_remote_cmd(f"cat {RPROC_STATE_PATH} 2>/dev/null", self.target_ssh).strip()
            self.rproc_state = state.upper() if state else "STOPPED"

            raw_trace = exec_remote_cmd(f"cat {TRACE0_PATH} 2>/dev/null", self.target_ssh)
            if raw_trace and raw_trace != last_trace:
                last_trace = raw_trace
                new_lines = [l for l in raw_trace.splitlines() if l.strip()]
                for l in new_lines[-20:]:
                    if l not in self.trace_logs:
                        self.trace_logs.append(l)
                        self.barectf_packet_count += 1
                if len(self.trace_logs) > 300:
                    self.trace_logs = self.trace_logs[-300:]
            time.sleep(0.8)

    def send_repl_command(self, code: str):
        if not code.strip():
            return
        self.repl_output_lines.append(f">>> {code}")
        out = send_code_to_e906(code, self.target_ssh)
        if out.strip():
            for line in out.splitlines():
                self.repl_output_lines.append(line)
        if len(self.repl_output_lines) > 500:
            self.repl_output_lines = self.repl_output_lines[-500:]

    def draw(self, stdscr):
        import curses
        curses.curs_set(1)
        curses.start_color()
        curses.use_default_colors()

        curses.init_pair(1, curses.COLOR_BLACK, curses.COLOR_CYAN)
        curses.init_pair(2, curses.COLOR_GREEN, -1)
        curses.init_pair(3, curses.COLOR_RED, -1)
        curses.init_pair(4, curses.COLOR_YELLOW, -1)
        curses.init_pair(5, curses.COLOR_CYAN, -1)
        curses.init_pair(6, curses.COLOR_WHITE, curses.COLOR_BLUE)
        curses.init_pair(7, curses.COLOR_BLACK, curses.COLOR_WHITE)

        stdscr.nodelay(True)
        stdscr.timeout(100)

        while self.running:
            stdscr.erase()
            h, w = stdscr.getmaxyx()
            if h < 16 or w < 60:
                stdscr.addstr(0, 0, "Terminal too small. Please enlarge.", curses.color_pair(3))
                stdscr.refresh()
                time.sleep(0.2)
                continue

            target_str = self.target_ssh if self.target_ssh else "Local Cubie A5E"
            header_text = f" AbstractX E906 Terminal Studio | Target: {target_str} (Pure SRAM, 0 DDR) "
            stdscr.attron(curses.color_pair(1) | curses.A_BOLD)
            stdscr.addstr(0, 0, header_text.ljust(w))
            stdscr.attroff(curses.color_pair(1) | curses.A_BOLD)

            status_col = curses.color_pair(2) if self.rproc_state == "RUNNING" else curses.color_pair(3)
            stdscr.addstr(1, 2, "RemoteProc: ", curses.A_DIM)
            stdscr.addstr(1, 14, f"[{self.rproc_state}]", status_col | curses.A_BOLD)

            stdscr.addstr(1, 28, "BareCTF Packets: ", curses.A_DIM)
            stdscr.addstr(1, 45, f"{self.barectf_packet_count}", curses.color_pair(5) | curses.A_BOLD)

            stdscr.addstr(1, 56, "Core: ", curses.A_DIM)
            stdscr.addstr(1, 62, "XuanTie E906 RV32", curses.A_NORMAL)
            stdscr.addstr(1, w - 18, "[F1/? for Help]", curses.color_pair(4))

            tabs = [
                (1, " [1] MicroPython REPL "),
                (2, " [2] BareCTF / Trace Stream "),
                (3, " [3] Hardware Monitor (GPS & Compass) "),
            ]
            col = 2
            for tab_id, name in tabs:
                if tab_id == self.active_tab:
                    stdscr.addstr(3, col, name, curses.color_pair(4) | curses.A_BOLD | curses.A_REVERSE)
                else:
                    stdscr.addstr(3, col, name, curses.A_DIM)
                col += len(name) + 2

            stdscr.hline(4, 0, curses.ACS_HLINE, w)
            main_h = h - 7

            if self.active_tab == 1:
                visible_lines = self.repl_output_lines[-main_h:]
                for idx, line in enumerate(visible_lines):
                    stdscr.addstr(5 + idx, 2, line[: w - 4])
                stdscr.hline(h - 2, 0, curses.ACS_HLINE, w)
                stdscr.addstr(h - 1, 2, ">>> ", curses.color_pair(4) | curses.A_BOLD)
                stdscr.addstr(h - 1, 6, self.repl_input_buf[-(w - 8) :])
                stdscr.move(h - 1, 6 + len(self.repl_input_buf[-(w - 8) :]))

            elif self.active_tab == 2:
                stdscr.addstr(5, 2, "Live Trace Stream (RPMsg Endpoint 1025 & debugfs trace0):", curses.color_pair(5) | curses.A_BOLD)
                trace_h = main_h - 2
                visible_trace = self.trace_logs[-trace_h:]
                if not visible_trace:
                    stdscr.addstr(7, 4, "(Waiting for telemetry packets / trace frames...)", curses.A_DIM)
                else:
                    for idx, line in enumerate(visible_trace):
                        stdscr.addstr(7 + idx, 2, f"▶ {line}"[: w - 4], curses.color_pair(2))

            elif self.active_tab == 3:
                stdscr.addstr(5, 2, "Hardware Peripherals & Sensor Ingestion (XuanTie E906 Direct MMIO):", curses.color_pair(5) | curses.A_BOLD)
                stdscr.addstr(7, 4, "┌─ u-blox NEO-M8N GPS (UART2 @ 9600 baud, Pins 11 & 13) ────┐", curses.color_pair(4))
                stdscr.addstr(8, 4, f"│ Status: {self.gps_info['status']:<15} Satellites: {self.gps_info['sats']:<15} │")
                stdscr.addstr(9, 4, f"│ MMIO  : UART2_BASE = 0x02500800                            │")
                stdscr.addstr(10, 4, "└───────────────────────────────────────────────────────────┘", curses.color_pair(4))

                stdscr.addstr(12, 4, "┌─ Electronic Compass / Magnetometer (I2C1, Pins 3 & 5) ─────┐", curses.color_pair(5))
                stdscr.addstr(13, 4, f"│ Addr  : {self.compass_info['addr']:<15} Status    : {self.compass_info['status']:<15} │")
                stdscr.addstr(14, 4, f"│ MMIO  : TWI1_BASE  = 0x02502400                            │")
                stdscr.addstr(15, 4, "└───────────────────────────────────────────────────────────┘", curses.color_pair(5))

            if self.active_modal:
                self._draw_modal(stdscr, h, w)

            stdscr.refresh()
            try:
                ch = stdscr.getch()
            except Exception:
                ch = -1
            if ch != -1:
                self._handle_input(ch)

    def _draw_modal(self, stdscr, h, w):
        import curses
        mw, mh = min(68, w - 6), min(18, h - 4)
        mx, my = (w - mw) // 2, (h - mh) // 2
        modal_win = curses.newwin(mh, mw, my, mx)
        modal_win.bkgd(" ", curses.color_pair(7))
        modal_win.box()

        if self.active_modal == "help":
            modal_win.addstr(0, 2, " Help & Keyboard Shortcuts ", curses.color_pair(6) | curses.A_BOLD)
            modal_win.addstr(2, 3, "Navigation & Views:", curses.A_BOLD)
            modal_win.addstr(3, 5, "1 / 2 / 3   : Switch Tabs (REPL / Trace / Hardware)")
            modal_win.addstr(4, 5, "F2 / 'G'    : Pop-up GPS Navigation Monitor")
            modal_win.addstr(5, 5, "F3 / 'C'    : Pop-up I2C Compass Scanner")
            modal_win.addstr(6, 5, "F4 / 'M'    : Pop-up E906 SRAM Memory Map")
            modal_win.addstr(7, 5, "Esc / 'q'   : Close active pop-up modal")
            modal_win.addstr(9, 3, "MicroPython REPL (Tab 1):", curses.A_BOLD)
            modal_win.addstr(10, 5, "import abstractx; print(abstractx.alive())")
            modal_win.addstr(11, 5, "hex(abstractx.peek(0x02500800))  # UART2 GPS")
            modal_win.addstr(12, 5, "hex(abstractx.peek(0x02502400))  # TWI1 I2C")
            modal_win.addstr(14, 3, "Press [Esc] or Enter to close.", curses.A_DIM)
        elif self.active_modal == "gps":
            modal_win.addstr(0, 2, " GPS Hardware Diagnostic (UART2) ", curses.color_pair(6) | curses.A_BOLD)
            modal_win.addstr(2, 3, "Physical Pins : Pin 11 (UART2-TX) -> Yellow (GPS RX)", curses.A_BOLD)
            modal_win.addstr(3, 3, "                Pin 13 (UART2-RX) -> Green (GPS TX)", curses.A_BOLD)
            modal_win.addstr(4, 3, "Power Wiring  : Pin 2 (5V Red) | Pin 6 (GND Black)")
            modal_win.addstr(6, 3, "Baud Rate     : 9600 baud (u-blox default)")
            modal_win.addstr(7, 3, "Hardware MMIO : UART2_BASE = 0x02500800")
            modal_win.addstr(9, 3, "Press [Esc] to return.", curses.A_DIM)
        elif self.active_modal == "compass":
            modal_win.addstr(0, 2, " Electronic Compass & I2C1 Prober ", curses.color_pair(6) | curses.A_BOLD)
            modal_win.addstr(2, 3, "Physical Pins : Pin 3 (I2C1-SDA) -> Blue (GPS/Mag SDA)", curses.A_BOLD)
            modal_win.addstr(3, 3, "                Pin 5 (I2C1-SCL) -> Orange (GPS/Mag SCL)", curses.A_BOLD)
            modal_win.addstr(5, 3, "Detected Addr : 0x1E (HMC5883L) / 0x0D (QMC5883L) / 0x42 (u-blox DDC)")
            modal_win.addstr(7, 3, "Hardware MMIO : TWI1_BASE = 0x02502400")
            modal_win.addstr(9, 3, "Press [Esc] to return.", curses.A_DIM)
        elif self.active_modal == "memmap":
            modal_win.addstr(0, 2, " XuanTie E906 Pure SRAM Memory Map ", curses.color_pair(6) | curses.A_BOLD)
            modal_win.addstr(2, 3, "Space 0 (192 KB) : 0x3FFC0000 -> Host 0x07280000 (Firmware)", curses.A_BOLD)
            modal_win.addstr(3, 3, "DSP SRAM (128 KB): 0x00020000 (Quarantined MicroPython GC)")
            modal_win.addstr(4, 3, "Trace0 Buffer    : 0x3FFEC88C (debugfs remoteproc0/trace0)")
            modal_win.addstr(5, 3, "DDR Usage        : 0 BYTES (100% Zero-DDR Bare-Metal)")
            modal_win.addstr(7, 3, "Press [Esc] to return.", curses.A_DIM)

        modal_win.refresh()

    def _handle_input(self, ch: int):
        import curses
        if self.active_modal:
            if ch in (27, ord("q"), ord("Q"), 10, 13):
                self.active_modal = None
            return

        if ch in (ord("?"), curses.KEY_F1):
            self.active_modal = "help"
        elif ch in (ord("G"), ord("g"), curses.KEY_F2):
            self.active_modal = "gps"
        elif ch in (ord("C"), ord("c"), curses.KEY_F3):
            self.active_modal = "compass"
        elif ch in (ord("M"), ord("m"), curses.KEY_F4):
            self.active_modal = "memmap"
        elif ch == ord("1"):
            self.active_tab = 1
        elif ch == ord("2"):
            self.active_tab = 2
        elif ch == ord("3"):
            self.active_tab = 3
        elif self.active_tab == 1:
            if ch in (10, 13, curses.KEY_ENTER):
                cmd = self.repl_input_buf
                self.repl_input_buf = ""
                self.send_repl_command(cmd)
            elif ch in (127, 8, curses.KEY_BACKSPACE):
                if self.repl_input_buf:
                    self.repl_input_buf = self.repl_input_buf[:-1]
            elif 32 <= ch <= 126:
                self.repl_input_buf += chr(ch)


def main():
    parser = argparse.ArgumentParser(description="AbstractX XuanTie E906 Unified Tool (TUI & CLI)")
    parser.add_argument("-c", "--command", help="Execute inline MicroPython command on E906 and exit")
    parser.add_argument("script", nargs="?", help="Execute Python script file on E906 and exit")
    parser.add_argument("--peek", help="Peek 32-bit MMIO/SRAM register on E906 (hex) and exit")
    parser.add_argument("--target", default="root@192.168.3.3", help="Target SSH host (or 'local' if on Cubie A5E)")
    args = parser.parse_args()

    target_ssh = None if args.target.lower() == "local" else args.target

    # Headless CLI Execution
    if args.peek:
        addr = int(args.peek, 0)
        code = f"import abstractx; print('0x{addr:08x}:', hex(abstractx.peek(0x{addr:08x})))"
        print(f"[*] Reading E906 MMIO 0x{addr:08x}...")
        resp = send_code_to_e906(code, target_ssh)
        print(resp.strip())
        sys.exit(0)
    elif args.command:
        resp = send_code_to_e906(args.command, target_ssh)
        print(resp.strip())
        sys.exit(0)
    elif args.script:
        with open(args.script, "r") as f:
            code = f.read()
        resp = send_code_to_e906(code, target_ssh)
        print(resp.strip())
        sys.exit(0)

    # Interactive TUI Studio
    try:
        import curses
        workbench = E906WorkbenchTUI(target_ssh=target_ssh)
        workbench.poll_thread.start()
        curses.wrapper(workbench.draw)
    except (KeyboardInterrupt, ImportError):
        pass


if __name__ == "__main__":
    main()
