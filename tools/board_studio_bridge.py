#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-USER-PLUGIN-01] [SPEC-E906-IPC-04] tools/board_studio_bridge.py
"""
board_studio_bridge.py - Live Board Telemetry Bridge to AbstractX Studio

Bridges raw sensor streams from XuanTie E906 (/dev/rpmsg0) on Radxa Cubie A5E
to AbstractX Studio listening on UDP port 9870 (192.168.3.1).
"""

import sys
import os
import time
import socket
import struct
import math
import subprocess
import signal
import threading
import asyncio

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [_HERE, os.path.join(_HERE, "..", "apps", "e906_repl_app")]
# The app's sensor format (copied next to this file on the board); AbstractX does not define it.
from sensor_contract import decode_mag, encode_mag_payload, nmea_sentence_ok, clamp_i32  # noqa: E402

HOST_IP = os.environ.get("STUDIO_HOST_IP", "192.168.3.1")
HOST_PORT = int(os.environ.get("STUDIO_PORT", 9870))
DURATION_S = float(os.environ.get("STREAM_DURATION", 0.0))

def parse_nmea_lat_lon(val_str, hemi):
    if not val_str or "." not in val_str:
        return 0.0
    dot_idx = val_str.index(".")
    deg_len = dot_idx - 2
    if deg_len <= 0:
        return 0.0
    deg = float(val_str[:deg_len])
    mins = float(val_str[deg_len:])
    res = deg + mins / 60.0
    if hemi in ("S", "W"):
        res = -res
    return res

def find_repl_node():
    """/dev/rpmsgN of the 'abstractx-repl' endpoint (never the TLP/trace nodes)."""
    import glob
    for d in sorted(glob.glob("/sys/class/rpmsg/rpmsg[0-9]*")):
        try:
            if open(d + "/name").read().strip() == "abstractx-repl":
                return "/dev/" + os.path.basename(d)
        except OSError:
            pass
    return None


def main():
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    main_task = loop.create_task(async_main())

    def on_term():
        main_task.cancel()

    for s in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(s, on_term)
        except (NotImplementedError, RuntimeError):
            pass

    try:
        loop.run_until_complete(main_task)
    except (asyncio.CancelledError, KeyboardInterrupt):
        pass
    finally:
        try:
            loop.run_until_complete(loop.shutdown_asyncgens())
        except Exception:
            pass
        loop.close()


async def async_main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    print(f"[BRIDGE] Forwarding live telemetry to AbstractX Studio at {HOST_IP}:{HOST_PORT} (asyncio mode)...")

    # Ensure /dev/rpmsg0 is not locked by a stale process
    repl_node = find_repl_node()
    if repl_node:
        os.system(f"fuser -k {repl_node} 2>/dev/null")
    await asyncio.sleep(0.2)

    # Launch E906 raw sensor streamer via verified RPMsg runner
    # Pass the duration through: run_e906_script.py defaults to 15 s, which ended every
    # live Studio session after 15 s. 0 = until stopped. @impl [SPEC-E906-REPL-14]
    cmd = ["python3", "-u", os.path.join(_HERE, "run_e906_script.py"), os.path.join(_HERE, "raw_sensor_streamer.py"),
           str(DURATION_S + 5.0 if DURATION_S > 0 else 0)]
    proc = await asyncio.create_subprocess_exec(
        *cmd,
        stdin=asyncio.subprocess.PIPE,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.STDOUT,
    )

    # @impl [SPEC-E906-REPL-14] apps/e906_repl_app/SPECIFICATION.md
    def interrupt_e906():
        try:
            if proc.stdin and not proc.stdin.is_closing():
                proc.stdin.write(b"STOP\n\x03\x03\x02")
        except Exception:
            pass
        try:
            proc.terminate()
        except Exception:
            pass
        try:
            proc.kill()
        except Exception:
            pass
        # Guarantee E906 script loop is interrupted so RPMsg messages cease
        try:
            node = find_repl_node()
            if node:
                os.system(f"fuser -k {node} 2>/dev/null")
                with open(node, "wb", buffering=0) as f:
                    f.write(b'\x03\x03\x02')
        except Exception:
            pass

    async def heartbeat_loop():
        """Periodic independent watchdog heartbeat to XuanTie E906."""
        while proc.returncode is None:
            await asyncio.sleep(1.0)
            try:
                if proc.stdin and not proc.stdin.is_closing():
                    proc.stdin.write(b"PING\n")
                    await proc.stdin.drain()
            except Exception:
                break

    stdin_queue: asyncio.Queue[str | None] = asyncio.Queue()
    loop = asyncio.get_running_loop()

    # Studio's stdin: "CLI <id> <hex>" lines are forwarded to the streamer as EXEC commands.
    # @impl [SPEC-E906-REPL-19] apps/e906_repl_app/SPECIFICATION.md
    def _read_stdin():
        for raw in sys.stdin:
            loop.call_soon_threadsafe(stdin_queue.put_nowait, raw)
        loop.call_soon_threadsafe(stdin_queue.put_nowait, None)

    threading.Thread(target=_read_stdin, daemon=True).start()

    async def stdin_dispatch_loop():
        while proc.returncode is None:
            raw = await stdin_queue.get()
            if raw is None:
                if os.environ.get("EXIT_ON_STDIN_EOF") == "1":
                    interrupt_e906()
                    return
                break
            parts = raw.split()
            if len(parts) == 3 and parts[0] == "CLI":
                try:
                    if proc.stdin and not proc.stdin.is_closing():
                        proc.stdin.write(f"EXEC {parts[1]} {parts[2]}\n".encode())
                        await proc.stdin.drain()
                except Exception:
                    pass
            stdin_queue.task_done()

    hb_task = asyncio.create_task(heartbeat_loop())
    stdin_task = asyncio.create_task(stdin_dispatch_loop())
    cli_open = None  # id of the CLI command whose output is arriving

    print("[BRIDGE] E906 raw_sensor_streamer running on XuanTie E906. Streaming to Studio...")

    seq = 0
    gps_nmea_acc = ""
    start_time = time.time()

    gps_lat = 0.0
    gps_lon = 0.0
    gps_alt = 0.0
    gps_spd = 0.0
    nmea_rejected = 0
    gps_sats = 0
    gps_fix = 0

    try:
        while DURATION_S <= 0 or (time.time() - start_time < DURATION_S):
            line_bytes = await proc.stdout.readline()
            if not line_bytes:
                if proc.returncode is not None:
                    break
                await asyncio.sleep(0.01)
                continue

            line = line_bytes.decode(errors="replace").strip()
            # CLI output framed by the streamer: passed to Studio on stdout ([SPEC-E906-REPL-19]).
            cli_at = line.find("!CLI:")
            if cli_at >= 0:
                line = line[cli_at:]
                fields = line.split(":")
                cli_open = fields[1] if len(fields) > 2 and fields[2] == "BEGIN" else None
                print(line, flush=True)
                continue
            if cli_open is not None:
                print(f"!CLI:{cli_open}:OUT:{line}", flush=True)
                continue
            if "!RAW:M=" in line:
                raw_idx = line.find("!RAW:M=")
                line = line[raw_idx:]
                parts = line[7:].split(":")
                mag_hex = parts[0] if len(parts) > 0 else ""
                fields = dict(p.split("=", 1) for p in parts[1:] if "=" in p)
                gps_hex = fields.get("G", "")
                mag_chip = fields.get("C", "QMC5883L")  # untagged lines come from older streamers

                # 1. Process Magnetometer
                heading_deg = 0.0
                raw_mag, mag_x, mag_y, mag_z = b"\x00" * 6, 0, 0, 0
                if len(mag_hex) == 12:
                    try:
                        raw_mag = bytes.fromhex(mag_hex)
                        x, y, z = decode_mag(raw_mag, mag_chip)
                        mag_x, mag_y, mag_z = x, y, z
                        heading_deg = (math.degrees(math.atan2(y, x)) + 360.0) % 360.0
                    except Exception:
                        pass

                # 2. Process GPS Stream
                if gps_hex:
                    try:
                        nmea_chars = bytes.fromhex(gps_hex).decode("ascii", errors="ignore")
                        gps_nmea_acc += nmea_chars
                        while "\n" in gps_nmea_acc:
                            sentence, gps_nmea_acc = gps_nmea_acc.split("\n", 1)
                            sentence = sentence.strip()
                            if not nmea_sentence_ok(sentence):
                                nmea_rejected += 1  # garbled: never let it reach a packet
                                continue
                            if sentence.startswith("$GNGGA") or sentence.startswith("$GPGGA"):
                                flds = sentence.split(",")
                                if len(flds) >= 10:
                                    if flds[2] and flds[3]:
                                        gps_lat = parse_nmea_lat_lon(flds[2], flds[3])
                                    if flds[4] and flds[5]:
                                        gps_lon = parse_nmea_lat_lon(flds[4], flds[5])
                                    if flds[6]:
                                        gps_fix = int(flds[6])
                                    if flds[7]:
                                        gps_sats = int(flds[7])
                                    if flds[9]:
                                        gps_alt = float(flds[9])
                            elif sentence.startswith("$GNRMC") or sentence.startswith("$GPRMC"):
                                flds = sentence.split(",")
                                if len(flds) >= 8 and flds[7]:
                                    try:
                                        gps_spd = float(flds[7]) * 0.514444 # knots to m/s
                                    except ValueError:
                                        pass
                    except Exception:
                        pass

                seq = (seq + 1) & 0xFFFF
                ts = int(time.time() * 1e9)

                # Synthesize 64B TLP Tag 5 (Live Hardware Attitude)
                hdr_att = struct.pack("<BBBBIHHQ", 0x10, 0x00, 5, 2, 0x40000100, 16, seq, ts)
                roll_cdeg = 0
                pitch_cdeg = 0
                yaw_cdeg = max(0, min(36000, int(heading_deg * 100)))
                alt_mm = int(gps_alt * 1000)
                spd_cm_s = max(0, min(65535, int(gps_spd * 100)))
                m1 = 500
                m2 = 500
                m3 = 500
                m4 = 500
                payload_att = struct.pack("<2hHiH4H", roll_cdeg, pitch_cdeg, yaw_cdeg, alt_mm, spd_cm_s, m1, m2, m3, m4)
                frame_att = hdr_att + payload_att + (b"\x00" * (40 - len(payload_att))) + b"\xde\xad\xbe\xef"
                sock.sendto(frame_att, (HOST_IP, HOST_PORT))

                # Synthesize 64B TLP Tag 2 (GPS Fix)
                hdr_gps = struct.pack("<BBBBIHHQ", 0x10, 0x00, 2, 2, 0x40000200, 26, seq, ts)
                itow = int(time.time() * 1000) % 604800000
                lat_i = int(gps_lat * 1e7)
                lon_i = int(gps_lon * 1e7)
                alt_i = int(gps_alt * 1000)
                spd_i = max(0, int(gps_spd * 1000))
                head_i = int(heading_deg * 100000)
                sats_b = max(0, min(255, int(gps_sats)))
                fix_b = max(0, min(255, int(gps_fix)))
                payload_gps = struct.pack("<i5i2B", *(clamp_i32(v) for v in (itow, lat_i, lon_i, alt_i, spd_i, head_i)),
                                          sats_b, fix_b)
                frame_gps = hdr_gps + payload_gps + (b"\x00" * (40 - len(payload_gps))) + b"\xde\xad\xbe\xef"
                sock.sendto(frame_gps, (HOST_IP, HOST_PORT))

                # Synthesize 64B TLP Tag 3 (Mag count trigger)
                # Tag 3: the compass registers exactly as the E906 read them, a chip code
                # (1 HMC5883L, 2 QMC5883L), a pad byte and the decoded X/Y/Z: 14 bytes, 4 DW.
                payload_mag = encode_mag_payload(raw_mag, mag_chip) if any(raw_mag) else b"\x00" * 14
                hdr_mag = struct.pack("<BBBBIHHQ", 0x10, 0x00, 3, 2, 0x40000300, (len(payload_mag) + 3) // 4, seq, ts)
                frame_mag = hdr_mag + payload_mag + b"\x00" * (40 - len(payload_mag)) + b"\xde\xad\xbe\xef"
                sock.sendto(frame_mag, (HOST_IP, HOST_PORT))

                if seq % 5 == 0:
                    print(f"[BRIDGE] Forwarded #{seq}: Heading={heading_deg:.1f}° Sats={gps_sats} Lat={gps_lat:.4f} Lon={gps_lon:.4f} Spd={gps_spd:.2f}m/s NMEA_bad={nmea_rejected}")
            else:
                if line:
                    print(f"[PROC] {line}")

    finally:
        hb_task.cancel()
        stdin_task.cancel()
        interrupt_e906()
        try:
            await asyncio.wait_for(proc.wait(), timeout=1.0)
        except Exception:
            try:
                proc.kill()
            except Exception:
                pass
        sock.close()
        print("[BRIDGE] Streaming complete.")

if __name__ == "__main__":
    main()
