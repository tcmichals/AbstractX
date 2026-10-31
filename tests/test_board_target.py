"""Board address and identity check for host tools.

@verifies [SPEC-AUDIT-13] docs/SPECTRACE.md (one board address, right board)
"""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import board_target  # noqa: E402


def test_tests_get_no_board_unless_asked(monkeypatch):
    monkeypatch.delenv(board_target.ENV, raising=False)
    assert board_target.board_ip(default=None) is None
    assert board_target.board_ip() == board_target.DEFAULT_BOARD_IP


def test_env_wins(monkeypatch):
    monkeypatch.setenv(board_target.ENV, "10.0.0.7")
    assert board_target.board_ip(default=None) == "10.0.0.7"


@pytest.mark.parametrize("model", ["Radxa Cubie A7A", "", "Raspberry Pi 5"])
def test_require_a5e_stops_on_another_board(monkeypatch, model):
    monkeypatch.setattr(board_target, "board_model", lambda ip, timeout_s=10.0: model)
    with pytest.raises(SystemExit):
        board_target.require_a5e("192.168.3.3")


def test_require_a5e_accepts_the_a5e(monkeypatch):
    monkeypatch.setattr(board_target, "board_model", lambda ip, timeout_s=10.0: "Radxa Cubie A5E")
    board_target.require_a5e("192.168.1.13")
