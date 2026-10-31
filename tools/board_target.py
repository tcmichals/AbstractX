"""Which board host tools talk to, and a check that it is the Cubie A5E.

@impl [SPEC-AUDIT-13] docs/SPECTRACE.md (one board address, right board)
"""
from __future__ import annotations

import os
import subprocess

ENV = "ABSTRACTX_BOARD_IP"
# GUI fallback only; tests never use it. The A5E is on Wi-Fi (DHCP) since 2026-10-31;
# Ethernet 192.168.3.3 is now the Cubie A7A.
DEFAULT_BOARD_IP = "192.168.1.13"
EXPECTED_MODEL = "Cubie A5E"


def board_ip(default: str | None = DEFAULT_BOARD_IP) -> str | None:
    """ABSTRACTX_BOARD_IP, else `default` (pass None in tests: no board unless asked)."""
    return os.environ.get(ENV) or default


def board_model(ip: str, timeout_s: float = 10.0) -> str:
    """Device-tree model of the board at `ip`, or "" if it cannot be read."""
    try:
        out = subprocess.run(
            ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", f"root@{ip}",
             "tr -d '\\0' < /proc/device-tree/model"],
            capture_output=True, text=True, timeout=timeout_s)
    except (OSError, subprocess.TimeoutExpired):
        return ""
    return out.stdout.strip() if out.returncode == 0 else ""


def require_a5e(ip: str) -> None:
    """Stop before changing board state unless `ip` is the Cubie A5E."""
    model = board_model(ip)
    if EXPECTED_MODEL not in model:
        raise SystemExit(f"{ip} is not the {EXPECTED_MODEL} (model: {model or 'unreadable'}); "
                         f"set {ENV} to the A5E's address")


def host_ip(board: str | None = None) -> str:
    """STUDIO_HOST_IP, else the local address this PC uses to reach the board (telemetry target)."""
    env = os.environ.get("STUDIO_HOST_IP")
    if env:
        return env
    import socket
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect((board or board_ip() or DEFAULT_BOARD_IP, 9))  # no packet is sent
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()
