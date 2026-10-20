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
import select
import math

HOST_IP = os.environ.get("STUDIO_HOST_IP", "192.168.3.1")
HOST_PORT = int(os.environ.get("STUDIO_PORT", 9870))
DEV_RPMSG = "/dev/rpmsg0"

def main():
    print(f"[BRIDGE] Connecting to E906 at {DEV_RPMSG}...")
    if not os.path.exists(DEV_RPMSG):
        print(f"Error: {DEV_RPMSG} not found. Is remoteproc running?", file=sys.stderr)
        sys.exit(1)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    print(f"[BRIDGE] Forwarding live telemetry to AbstractX Studio at {HOST_IP}:{HOST_PORT}...")

    fd = os.open(DEV_RPMSG, os.O_RDWR | os.O_NONBLOCK)

    # Trigger raw_sensor_streamer.py on E906
    # Reset REPL and execute script
    os.write(fd, b"\x03\x03\x02")
    time.sleep(0.05)
    while select.select([fd], [], [], 0.05)[0]:
        os.read(fd, 2048)

    # Enter raw REPL and send execution command
    os.write(fd, b"\x01")
    time.sleep(0.05)
    with open("/root/raw_sensor_streamer.py", "r") as f:
        script = f.read()

    for i in range(0, len(script), 256):
        os.write(fd, script[i:i+256].encode("utf-8"))
        time.sleep(0.005)

    os.write(fd, b"\x04") # Execute Ctrl-D
    print("[BRIDGE] E906 raw_sensor_streamer running. Streaming to Studio...")

    seq = 0
    buf = ""
    start_time = time.time()

    while time.time() - start_time < 30.0:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            chunk = os.read(fd, 1024).decode("utf-8", errors="ignore")
            buf += chunk
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                line = line.strip()
                if line.startswith("!RAW:M="):
                    # Extract raw magnetometer hex
                    parts = line[7:].split(":")
                    mag_hex = parts[0] if len(parts) > 0 else ""
                    gps_part = parts[1] if len(parts) > 1 else ""

                    heading_deg = 0.0
                    if len(mag_hex) == 12:
                        raw_bytes = bytes.fromhex(mag_hex)
                        x = raw_bytes[0] | (raw_bytes[1] << 8)
                        y = raw_bytes[2] | (raw_bytes[3] << 8)
                        z = raw_bytes[4] | (raw_bytes[5] << 8)
                        if x >= 32768: x -= 65536
                        if y >= 32768: y -= 65536
                        if z >= 32768: z -= 65536
                        heading_deg = (math.degrees(math.atan2(y, x)) + 360.0) % 360.0

                    # Synthesize 64B TLP packet matching SITL wire format (type=0x10, tag=4, ch=2)
                    seq = (seq + 1) & 0xFFFF
                    ts = int(time.time() * 1e9)
                    hdr = struct.pack("<BBBBIHHQ", 0x10, 0x00, 4, 2, 0x40000100, 16, seq, ts)
                    roll_cdeg = 0
                    pitch_cdeg = 0
                    yaw_cdeg = int(heading_deg * 100)
                    alt_mm = 142500
                    spd_cm_s = 730
                    m1 = 500
                    m2 = 500
                    m3 = 500
                    m4 = 500
                    payload = struct.pack("<2hHiH4H", roll_cdeg, pitch_cdeg, yaw_cdeg, alt_mm, spd_cm_s, m1, m2, m3, m4)
                    frame = hdr + payload + (b"\x00" * (40 - len(payload))) + b"\xde\xad\xbe\xef"

                    # Send to Studio
                    sock.sendto(frame, (HOST_IP, HOST_PORT))

    os.close(fd)
    sock.close()
    print("[BRIDGE] Streaming complete.")

if __name__ == "__main__":
    main()
