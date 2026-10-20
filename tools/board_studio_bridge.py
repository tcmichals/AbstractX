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

HOST_IP = os.environ.get("STUDIO_HOST_IP", "192.168.3.1")
HOST_PORT = int(os.environ.get("STUDIO_PORT", 9870))
DURATION_S = float(os.environ.get("STREAM_DURATION", 60.0))

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

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    print(f"[BRIDGE] Forwarding live telemetry to AbstractX Studio at {HOST_IP}:{HOST_PORT}...")

    # Launch E906 raw sensor streamer via verified RPMsg runner
    cmd = ["python3", "/root/run_e906_script.py", "/root/raw_sensor_streamer.py"]
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1
    )

    print("[BRIDGE] E906 raw_sensor_streamer running on XuanTie E906. Streaming to Studio...")

    seq = 0
    gps_nmea_acc = ""
    start_time = time.time()

    gps_lat = 0.0
    gps_lon = 0.0
    gps_alt = 0.0
    gps_spd = 0.0
    gps_sats = 0
    gps_fix = 0

    try:
        while time.time() - start_time < DURATION_S:
            line = proc.stdout.readline()
            if not line:
                if proc.poll() is not None:
                    break
                time.sleep(0.01)
                continue

            line = line.strip()
            if "!RAW:M=" in line:
                raw_idx = line.find("!RAW:M=")
                line = line[raw_idx:]
                parts = line[7:].split(":")
                mag_hex = parts[0] if len(parts) > 0 else ""
                gps_hex = ""
                if len(parts) > 1 and parts[1].startswith("G="):
                    gps_hex = parts[1][2:]

                # 1. Process Magnetometer
                heading_deg = 0.0
                if len(mag_hex) == 12:
                    try:
                        raw_bytes = bytes.fromhex(mag_hex)
                        x = raw_bytes[0] | (raw_bytes[1] << 8)
                        y = raw_bytes[2] | (raw_bytes[3] << 8)
                        z = raw_bytes[4] | (raw_bytes[5] << 8)
                        if x >= 32768: x -= 65536
                        if y >= 32768: y -= 65536
                        if z >= 32768: z -= 65536
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
                payload_gps = struct.pack("<i5i2B", itow, lat_i, lon_i, alt_i, spd_i, head_i, sats_b, fix_b)
                frame_gps = hdr_gps + payload_gps + (b"\x00" * (40 - len(payload_gps))) + b"\xde\xad\xbe\xef"
                sock.sendto(frame_gps, (HOST_IP, HOST_PORT))

                # Synthesize 64B TLP Tag 3 (Mag count trigger)
                hdr_mag = struct.pack("<BBBBIHHQ", 0x10, 0x00, 3, 2, 0x40000300, 0, seq, ts)
                frame_mag = hdr_mag + (b"\x00" * 40) + b"\xde\xad\xbe\xef"
                sock.sendto(frame_mag, (HOST_IP, HOST_PORT))

                if seq % 5 == 0:
                    print(f"[BRIDGE] Forwarded #{seq}: Heading={heading_deg:.1f}° Sats={gps_sats} Lat={gps_lat:.4f} Lon={gps_lon:.4f} Spd={gps_spd:.2f}m/s")
            else:
                if line:
                    print(f"[PROC] {line}")

    finally:
        try:
            proc.terminate()
            proc.wait(timeout=1.0)
        except Exception:
            try:
                proc.kill()
            except Exception:
                pass
        sock.close()
        print("[BRIDGE] Streaming complete.")

if __name__ == "__main__":
    main()
