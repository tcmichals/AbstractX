"""Board address and identity check for host tools (plain python3; pytest also collects it).

@verifies [SPEC-AUDIT-13] docs/SPECTRACE.md (one board address, right board)
"""
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import board_target  # noqa: E402


def _with_env(value, fn):
    old = os.environ.get(board_target.ENV)
    try:
        if value is None:
            os.environ.pop(board_target.ENV, None)
        else:
            os.environ[board_target.ENV] = value
        fn()
    finally:
        if old is None:
            os.environ.pop(board_target.ENV, None)
        else:
            os.environ[board_target.ENV] = old


def _with_model(model, fn):
    real = board_target.board_model
    board_target.board_model = lambda ip, timeout_s=10.0: model
    try:
        fn()
    finally:
        board_target.board_model = real


def test_tests_get_no_board_unless_asked():
    def check():
        assert board_target.board_ip(default=None) is None
        assert board_target.board_ip() == board_target.DEFAULT_BOARD_IP
    _with_env(None, check)


def test_env_wins():
    _with_env("10.0.0.7", lambda: (board_target.board_ip(default=None) == "10.0.0.7") or (_ for _ in ()).throw(AssertionError("env ignored")))


def test_require_a5e_stops_on_another_board():
    for model in ("Radxa Cubie A7A", "", "Raspberry Pi 5"):
        def check():
            try:
                board_target.require_a5e("192.168.3.3")
            except SystemExit:
                return
            raise AssertionError(f"require_a5e accepted {model!r}")
        _with_model(model, check)


def test_require_a5e_accepts_the_a5e():
    _with_model("Radxa Cubie A5E", lambda: board_target.require_a5e("192.168.1.13"))


if __name__ == "__main__":
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for t in tests:
        t()
    print(f"test_board_target: {len(tests)} OK")
