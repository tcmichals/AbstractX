#!/usr/bin/env python3
"""
run_e906_script.py - Host / Target Runner for XuanTie E906 MicroPython REPL

@impl [SPEC-E906-REPL-06] apps/e906_repl_app/SPECIFICATION.md

Transfers a Python script over VirtIO RPMsg endpoint (/dev/rpmsg0) in 256-byte chunks,
executes it in raw REPL mode on the XuanTie E906 RISC-V co-processor, and streams stdout.
"""

import sys
import os
import time
import select
import glob
import struct
import fcntl

def ensure_rpmsg_device():
    devs = glob.glob('/dev/rpmsg[0-9]*')
    if not devs:
        if not os.path.exists('/dev/rpmsg_ctrl0'):
            print("Error: /dev/rpmsg_ctrl0 not found. Is remoteproc running?", file=sys.stderr)
            sys.exit(1)
        info = struct.pack('<32sII', b'abstractx-repl', 1026, 1026)
        fd = os.open('/dev/rpmsg_ctrl0', os.O_RDWR)
        try:
            # RPMSG_CREATE_EPT_IOCTL
            fcntl.ioctl(fd, (1 << 30) | (40 << 16) | (0xB5 << 8) | 0x1, info)
        finally:
            os.close(fd)
        time.sleep(0.1)
        devs = glob.glob('/dev/rpmsg[0-9]*')
        if not devs:
            print("Error: Failed to create RPMsg endpoint /dev/rpmsg0", file=sys.stderr)
            sys.exit(1)
    return devs[0]

def run_script(script_path, timeout=15.0):
    if not os.path.exists(script_path):
        print(f"Error: Script file '{script_path}' not found.", file=sys.stderr)
        sys.exit(1)

    with open(script_path, 'r') as f:
        code = f.read()

    target_dev = ensure_rpmsg_device()
    fd = os.open(target_dev, os.O_RDWR | os.O_NONBLOCK)

    try:
        # 1. Reset any running script and exit to normal REPL
        os.write(fd, b'\x03\x03\x02')
        time.sleep(0.05)
        while select.select([fd], [], [], 0.05)[0]:
            os.read(fd, 2048)

        # 2. Enter raw REPL: Ctrl-A
        os.write(fd, b'\x01')
        time.sleep(0.05)
        while select.select([fd], [], [], 0.05)[0]:
            os.read(fd, 2048)

        # 3. Send script in 256-byte chunks (matches VirtIO RPMsg buffer limits)
        payload = code.strip().encode('utf-8') + b'\x04'
        chunk_size = 256
        for i in range(0, len(payload), chunk_size):
            chunk = payload[i:i + chunk_size]
            os.write(fd, chunk)
            time.sleep(0.02)

        # 4. Stream response from MicroPython VM
        start_t = time.time()
        in_raw = True
        while time.time() - start_t < timeout:
            r, _, _ = select.select([fd], [], [], 0.3)
            if r:
                data = os.read(fd, 2048)
                if in_raw and b'OK' in data:
                    data = data.split(b'OK', 1)[1]
                    in_raw = False
                if b'\x04' in data:
                    # End of execution marker
                    data = data.replace(b'\x04', b'')
                    sys.stdout.buffer.write(data)
                    sys.stdout.flush()
                    break
                sys.stdout.buffer.write(data)
                sys.stdout.flush()

        # 5. Exit raw REPL: Ctrl-B
        os.write(fd, b'\x02')
    finally:
        os.close(fd)

if __name__ == '__main__':
    script = sys.argv[1] if len(sys.argv) > 1 else 'flight_sensors.py'
    run_script(script)
