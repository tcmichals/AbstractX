#!/usr/bin/env python3
# @impl [SPEC-PICO-CFG-01] targets/pico2w_rp2350/SPECIFICATION.md
"""Build the Pico presets and check each image against its Wi-Fi option.

pico2w         Pico 2 W, Wi-Fi on : CYW43 symbols and __wrap_main present
pico2w-nowifi  Pico 2 W, Wi-Fi off: no cyw43_ / pbuf_ / udp_ / netif_ symbol, no __wrap_main
pico2          Pico 2,   Wi-Fi off: same as pico2w-nowifi
and: PICO_BOARD=pico2 with ABSTRACTX_PICO_WIFI=ON must fail to configure.

Exit 0 pass, 1 fail, 77 skipped (no Pico SDK or arm-none-eabi toolchain).
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APPS = ("serial_loopback_app", "gps_imu_app")
PRESETS = {  # preset -> (binary dir, Wi-Fi expected)
    "pico2w": ("build_pico2w", True),
    "pico2w-nowifi": ("build_pico2w_nowifi", False),
    "pico2": ("build_pico2", False),
}
NET_SYMBOL = re.compile(r"\s(cyw43_\w*|pbuf_\w*|udp_\w*|netif_\w*)$")
WRAP_MAIN = re.compile(r"\s__wrap_main$")


def network_symbols(nm_text: str) -> list[str]:
    """CYW43 / lwIP symbol names in `nm` output."""
    out = []
    for line in nm_text.splitlines():
        m = NET_SYMBOL.search(line.rstrip())
        if m:
            out.append(m.group(1))
    return out


def has_wrap_main(nm_text: str) -> bool:
    return any(WRAP_MAIN.search(line.rstrip()) for line in nm_text.splitlines())


# @impl [SPEC-PICO-CFG-01] targets/pico2w_rp2350/SPECIFICATION.md (the per-image Wi-Fi check)
def check_image(name: str, nm_text: str, wifi: bool) -> list[str]:
    """Problems with one image, given whether it was built with Wi-Fi on."""
    if not nm_text.strip():
        return [f"{name}: nm printed no symbols"]
    net = network_symbols(nm_text)
    wrap = has_wrap_main(nm_text)
    problems = []
    if wifi:
        if not any(s.startswith("cyw43_") for s in net):
            problems.append(f"{name}: Wi-Fi on, but no cyw43_ symbol")
        if not wrap:
            problems.append(f"{name}: Wi-Fi on, but no __wrap_main")
    else:
        if net:
            problems.append(f"{name}: Wi-Fi off, but {len(net)} network symbol(s), e.g. {', '.join(sorted(set(net))[:5])}")
        if wrap:
            problems.append(f"{name}: Wi-Fi off, but __wrap_main is linked")
    return problems


def find_tool(name: str) -> str | None:
    prefix = os.environ.get("ARM_TOOLCHAIN_PREFIX")
    if prefix and Path(prefix + name).exists():
        return prefix + name
    found = shutil.which("arm-none-eabi-" + name)
    if found:
        return found
    for cand in sorted(Path.home().glob(".tools/*arm-none-eabi*/bin/arm-none-eabi-" + name)) + \
            sorted(Path.home().glob(".tools/*/*arm-none-eabi*/bin/arm-none-eabi-" + name)):
        return str(cand)
    return None


def run(cmd: list[str], env: dict, log: Path) -> int:
    with log.open("w") as f:
        return subprocess.run(cmd, cwd=ROOT, env=env, stdout=f, stderr=subprocess.STDOUT).returncode


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--no-build", action="store_true", help="check existing images only")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 2)
    args = ap.parse_args()

    nm = find_tool("nm")
    sdk = os.environ.get("PICO_SDK_PATH") or next(
        (str(p) for p in (Path.home() / ".tools/pico-sdk", Path.home() / ".tools/pico/pico-sdk")
         if (p / "pico_sdk_init.cmake").exists()), None)
    if nm is None or sdk is None:
        print(f"[SKIP] pico-build: arm-none-eabi toolchain {'missing' if nm is None else 'found'}, "
              f"Pico SDK {'missing' if sdk is None else 'found'}")
        return 77
    env = dict(os.environ)
    env["PATH"] = str(Path(nm).parent) + os.pathsep + env.get("PATH", "")
    env.setdefault("PICO_SDK_PATH", sdk)

    problems: list[str] = []
    for preset, (bdir, wifi) in PRESETS.items():
        if not args.no_build:
            log = ROOT / f"{bdir}.check.log"
            (ROOT / bdir).mkdir(exist_ok=True)
            if run(["cmake", "--preset", preset], env, log) != 0 or \
               run(["cmake", "--build", "--preset", preset, "-j", str(args.jobs), "--target", *APPS], env, log.with_suffix(".build.log")) != 0:
                problems.append(f"{preset}: configure or build failed (log: {log.name}, {log.with_suffix('.build.log').name})")
                continue
        for app in APPS:
            elf = ROOT / bdir / "apps" / app / f"{app}.elf"
            if not elf.is_file() or elf.stat().st_size == 0:
                problems.append(f"{preset}: {elf.relative_to(ROOT)} missing or empty")
                continue
            res = subprocess.run([nm, str(elf)], capture_output=True, text=True)
            if res.returncode != 0:
                problems.append(f"{preset}: nm failed on {elf.name}: {res.stderr.strip()}")
                continue
            p = check_image(f"{preset}/{app}", res.stdout, wifi)
            problems += p
            print(f"   {'FAIL' if p else 'ok  '} {preset}/{app}: Wi-Fi {'on' if wifi else 'off'}, "
                  f"{len(network_symbols(res.stdout))} network symbols, __wrap_main {'yes' if has_wrap_main(res.stdout) else 'no'}, "
                  f"{elf.stat().st_size} B")

    if not args.no_build:
        with tempfile.TemporaryDirectory(prefix="abx_pico_neg_") as tmp:
            rc = run(["cmake", "-S", ".", "-B", tmp, f"-DCMAKE_TOOLCHAIN_FILE={ROOT}/cmake/toolchains/arm-none-eabi.cmake",
                      "-DABSTRACTX_TARGET=pico2w", "-DPICO_PLATFORM=rp2350-arm-s", "-DPICO_BOARD=pico2",
                      "-DABSTRACTX_PICO_WIFI=ON"], env, Path(tmp) / "cfg.log")
            text = (Path(tmp) / "cfg.log").read_text(errors="replace")
            ok = rc != 0 and "has no CYW43" in text
            print(f"   {'ok  ' if ok else 'FAIL'} pico2 + ABSTRACTX_PICO_WIFI=ON refused at configure (rc={rc})")
            if not ok:
                problems.append("pico2 with ABSTRACTX_PICO_WIFI=ON was not refused at configure")

    for p in problems:
        print(f"[FAIL] {p}")
    print(f"pico-build: {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
