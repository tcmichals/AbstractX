---
name: pico-build
runs_on: host
verifies: "The Pico 2 / Pico 2 W presets build, and each image matches its Wi-Fi option."
description: "Check skill. The Pico 2 / Pico 2 W presets build, and each image matches its Wi-Fi option. Run when a requirement names this skill in its Verified by line or a change touches targets/pico2w_rp2350/ or the Pico presets."
---

# Skill: pico-build

**What it verifies**: the `pico2w`, `pico2w-nowifi` and `pico2` presets configure and build
`serial_loopback_app` and `gps_imu_app`; each image matches its Wi-Fi option ([SPEC-PICO-CFG-01]):

| Preset | Board | Wi-Fi | Image must have |
| :--- | :--- | :--- | :--- |
| `pico2w` | `pico2_w` | on | `cyw43_` symbols and `__wrap_main` |
| `pico2w-nowifi` | `pico2_w` | off | no `cyw43_` / `pbuf_` / `udp_` / `netif_` symbol, no `__wrap_main` |
| `pico2` | `pico2` | off | the same as `pico2w-nowifi` |

It also configures `PICO_BOARD=pico2` with `ABSTRACTX_PICO_WIFI=ON` and requires that to fail.

## How to run

```bash
python3 tools/check_pico_build.py            # configure, build, check (a few minutes)
python3 tools/check_pico_build.py --no-build # check the images already built
python3 tests/test_check_pico_build.py       # the checks themselves, on fixed nm listings
```

The toolchain is found from `ARM_TOOLCHAIN_PREFIX`, then `PATH`, then `~/.tools/*arm-none-eabi*/bin`;
the SDK from `PICO_SDK_PATH`, then `~/.tools/pico-sdk` or `~/.tools/pico/pico-sdk`.

## Pass criteria

Exit 0. Exit 1 on any problem (a build that fails, an ELF that is missing or empty, `nm` failing, a
symbol check failing, the bad configuration accepted). Exit 77 when the toolchain or SDK is missing:
report it as SKIPPED, never as a pass.

## Evidence

The per-image lines it prints (network symbol count, `__wrap_main`, ELF size) go in the commit message.
