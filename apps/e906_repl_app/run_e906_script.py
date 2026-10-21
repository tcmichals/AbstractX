#!/usr/bin/env python3
"""
run_e906_script.py - Host / Target Runner for XuanTie E906 MicroPython REPL

@impl [SPEC-E906-REPL-06] [SPEC-E906-REPL-12] apps/e906_repl_app/SPECIFICATION.md

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
import signal

def _find_repl_node():
    """Return /dev/rpmsgN whose endpoint is 'abstractx-repl' (never a TLP/trace node)."""
    for d in sorted(glob.glob('/sys/class/rpmsg/rpmsg[0-9]*')):
        try:
            if open(d + '/name').read().strip() == 'abstractx-repl':
                return '/dev/' + os.path.basename(d)
        except OSError:
            pass
    return None

def ensure_rpmsg_device():
    # @impl [SPEC-E906-REPL-12] apps/e906_repl_app/SPECIFICATION.md
    node = _find_repl_node()
    if node:
        return node
    if not os.path.exists('/dev/rpmsg_ctrl0'):
        print("Error: /dev/rpmsg_ctrl0 not found. Is remoteproc running?", file=sys.stderr)
        sys.exit(1)
    info = struct.pack('<32sII', b'abstractx-repl', 1026, 1026)
    fd = os.open('/dev/rpmsg_ctrl0', os.O_RDWR)
    try:
        fcntl.ioctl(fd, (1 << 30) | (40 << 16) | (0xB5 << 8) | 0x1, info)
    finally:
        os.close(fd)
    for _ in range(20):
        time.sleep(0.1)
        node = _find_repl_node()
        if node:
            return node
    print("Error: Failed to create abstractx-repl endpoint", file=sys.stderr)
    sys.exit(1)

def run_script(script_path, timeout=15.0):
    if not os.path.exists(script_path):
        print(f"Error: Script file '{script_path}' not found.", file=sys.stderr)
        sys.exit(1)

    with open(script_path, 'r') as f:
        code = f.read()

    target_dev = ensure_rpmsg_device()
    try:
        fd = os.open(target_dev, os.O_RDWR | os.O_NONBLOCK)
    except OSError as err:
        if err.errno == 16:  # EBUSY: another process has /dev/rpmsg0 open
            os.system(f"fuser -k {target_dev} 2>/dev/null")
            time.sleep(0.3)
            fd = os.open(target_dev, os.O_RDWR | os.O_NONBLOCK)
        else:
            raise

    try:
        def write_all(buf):
            tot = 0
            while tot < len(buf):
                try:
                    n = os.write(fd, buf[tot:])
                    tot += n
                except (BlockingIOError, OSError):
                    time.sleep(0.01)

        def sig_handler(sig, frame):
            try:
                write_all(b'\x03\x03\x02')
                time.sleep(0.05)
            except Exception:
                pass
            try:
                os.close(fd)
            except Exception:
                pass
            sys.exit(0)

        signal.signal(signal.SIGINT, sig_handler)
        signal.signal(signal.SIGTERM, sig_handler)

        def drain_rx(max_chunks=30, timeout_s=0.05):
            for _ in range(max_chunks):
                if not select.select([fd], [], [], timeout_s)[0]:
                    break
                try:
                    os.read(fd, 2048)
                except (BlockingIOError, OSError):
                    break

        # 1. Reset any running script immediately and exit to normal REPL
        write_all(b'\x03\x03\x02')
        time.sleep(0.05)
        drain_rx(30, 0.05)

        # 2. Enter raw REPL: Ctrl-A
        write_all(b'\x01')
        time.sleep(0.05)
        drain_rx(10, 0.05)

        # 3. Send script in 256-byte chunks (matches VirtIO RPMsg buffer limits)
        payload = code.strip().encode('utf-8') + b'\x04'
        chunk_size = 256
        for i in range(0, len(payload), chunk_size):
            chunk = payload[i:i + chunk_size]
            write_all(chunk)
            time.sleep(0.02)

        # 4. Stream response from MicroPython VM (stdout and stderr)
        # @impl [SPEC-E906-REPL-14] apps/e906_repl_app/SPECIFICATION.md
        start_t = time.time()
        in_raw = True
        eot_count = 0
        stdin_fileno = None
        try:
            stdin_fileno = sys.stdin.fileno()
        except Exception:
            pass

        while time.time() - start_t < timeout:
            rlist = [fd]
            if stdin_fileno is not None:
                rlist.append(stdin_fileno)
            r, _, _ = select.select(rlist, [], [], 0.1)
            if fd in r:
                data = os.read(fd, 2048)
                if in_raw and b'OK' in data:
                    data = data.split(b'OK', 1)[1]
                    in_raw = False
                if b'\x04' in data:
                    eot_count += data.count(b'\x04')
                    data = data.replace(b'\x04', b'')
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
                    if eot_count >= 2:
                        break
                else:
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()

            if stdin_fileno is not None and stdin_fileno in r:
                try:
                    in_chunk = os.read(stdin_fileno, 512)
                    if in_chunk:
                        write_all(in_chunk)
                except Exception:
                    pass

        # 5. Exit raw REPL: Ctrl-B
        os.write(fd, b'\x02')
    finally:
        try:
            os.write(fd, b'\x03\x03\x02')
        except Exception:
            pass
        os.close(fd)

if __name__ == '__main__':
    script = sys.argv[1] if len(sys.argv) > 1 else 'flight_sensors.py'
    timeout = float(sys.argv[2]) if len(sys.argv) > 2 else 15.0
    run_script(script, timeout=timeout)
