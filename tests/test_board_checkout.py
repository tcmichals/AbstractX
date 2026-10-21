"""
test_board_checkout.py - Pytest Integration for Physical Board System Checkout
-------------------------------------------------------------------------------
# @impl [SPEC-STUDIO-08] tools/visualizer/SPECIFICATION.md
# @impl [SPEC-E906-REPL-14] apps/e906_repl_app/SPECIFICATION.md

Validates that when the physical Radxa Cubie A5E flight controller board
is connected to the test network (default 192.168.3.3), the target subsystems,
RPMsg endpoint, and E906 coprocessor are healthy and responsive.
"""

import os
import socket
import pytest
from pathlib import Path
from tools.test_board_system_checkout import run_board_ssh

BOARD_IP = os.environ.get("BOARD_IP", "192.168.3.3")


def is_board_reachable(ip: str) -> bool:
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(0.8)
        s.connect((ip, 22))
        s.close()
        return True
    except Exception:
        return False


@pytest.mark.board
def test_board_network_and_remoteproc():
    """Validates SSH connectivity and that RemoteProc0 XuanTie E906 is running."""
    if not is_board_reachable(BOARD_IP):
        pytest.skip(f"Board at {BOARD_IP} is not reachable over network.")

    res = run_board_ssh("cat /sys/class/remoteproc/remoteproc0/state", BOARD_IP, timeout=5.0)
    assert res.returncode == 0
    assert res.stdout.strip() == "running"


@pytest.mark.board
def test_board_rpmsg_and_uio_devices():
    """Validates that /dev/rpmsg0 and /sys/class/uio exist on the target Linux system."""
    if not is_board_reachable(BOARD_IP):
        pytest.skip(f"Board at {BOARD_IP} is not reachable over network.")

    res = run_board_ssh("ls -la /dev/rpmsg0 /sys/class/uio", BOARD_IP, timeout=5.0)
    assert res.returncode == 0
    assert "/dev/rpmsg0" in res.stdout
    assert "/sys/class/uio" in res.stdout


@pytest.mark.board
def test_board_e906_firmware_identity():
    """Validates that remoteproc is loaded with e906_repl_app.elf firmware."""
    if not is_board_reachable(BOARD_IP):
        pytest.skip(f"Board at {BOARD_IP} is not reachable over network.")

    res = run_board_ssh("cat /sys/class/remoteproc/remoteproc0/firmware", BOARD_IP, timeout=5.0)
    assert res.returncode == 0
    assert "e906_repl_app.elf" in res.stdout.strip()
