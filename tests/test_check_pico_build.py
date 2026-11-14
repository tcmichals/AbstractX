#!/usr/bin/env python3
# @verifies [SPEC-PICO-CFG-01] targets/pico2w_rp2350/SPECIFICATION.md
"""tools/check_pico_build.py symbol checks, on fixed nm listings (no toolchain needed)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import check_pico_build as c  # noqa: E402

WIFI_NM = """\
10000234 T main
100004a0 T __wrap_main
10001000 T cyw43_arch_init
10001100 T cyw43_arch_poll
10002000 T pbuf_alloc
10002100 T udp_sendto
20000400 B netif_default
"""
PLAIN_NM = """\
10000234 T main
10000300 T stdio_init_all
10000400 T _ZN9abstractx3hal13platform_initEv
20000000 B g_pico_spi
"""


def test_wifi_off_fails_on_a_wifi_image():
    p = c.check_image("x", WIFI_NM, wifi=False)
    assert any("network symbol" in s for s in p), p
    assert any("__wrap_main" in s for s in p), p


def test_wifi_off_passes_a_plain_image():
    assert c.check_image("x", PLAIN_NM, wifi=False) == []


def test_wifi_on_fails_on_a_plain_image():
    p = c.check_image("x", PLAIN_NM, wifi=True)
    assert any("no cyw43_" in s for s in p) and any("__wrap_main" in s for s in p), p


def test_wifi_on_passes_a_wifi_image():
    assert c.check_image("x", WIFI_NM, wifi=True) == []


def test_empty_listing_is_a_failure():
    assert c.check_image("x", "", wifi=False) != []
    assert c.check_image("x", "  \n", wifi=True) != []


def test_symbol_names_inside_other_names_do_not_count():
    # a symbol that only contains "udp_" (not starts with it) is not lwIP
    nm = "10000000 T my_udp_helper\n10000010 T trace_pbuf_shim\n"
    assert c.network_symbols(nm) == []


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print(f"[PASS] {name}")
