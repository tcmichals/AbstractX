"""Regression checks for source-controlled Zynq Buildroot configuration."""

import os
import re
from pathlib import Path


EXTERNAL_DIR = Path("hw/zynq7000/buildroot_external")
CONFIG_DIR = EXTERNAL_DIR / "configs"
HOOK_PATTERN = re.compile(
    r'^BR2_ROOTFS_POST_(?:BUILD|IMAGE)_SCRIPT="\$\(BR2_EXTERNAL_ABSTRACTX_ZYNQ7000_PATH\)/(.+)"$'
)


def _config_lines(path: Path) -> set[str]:
    return set(path.read_text(encoding="utf-8").splitlines())


# @impl [SPEC-ZYNQ-PLATFORM-07] hw/zynq7000/SPECIFICATION.md
def test_configured_buildroot_hooks_are_executable(repo_root: Path) -> None:
    external_dir = repo_root / EXTERNAL_DIR
    configs = sorted((repo_root / CONFIG_DIR).glob("abstractx_*_defconfig"))
    assert configs, "No supported Zynq Buildroot defconfigs found"

    for config in configs:
        hooks = [
            match.group(1)
            for line in config.read_text(encoding="utf-8").splitlines()
            if (match := HOOK_PATTERN.fullmatch(line))
        ]
        assert hooks, f"No Buildroot hooks found in {config}"
        for relative_hook in hooks:
            hook = external_dir / relative_hook
            assert hook.is_file(), f"Configured Buildroot hook is not a file: {hook}"
            assert os.access(hook, os.X_OK), f"Configured Buildroot hook is not executable: {hook}"


# @impl [SPEC-AC7020C-08] hw/zynq7000/alinx_ac7020c/SPECIFICATION.md
def test_alinx_ac7020c_linux_gpio_configuration(repo_root: Path) -> None:
    config = _config_lines(
        repo_root / CONFIG_DIR / "abstractx_alinx_ac7020c_defconfig"
    )
    required_config = {
        "BR2_PACKAGE_HOST_LINUX_HEADERS_CUSTOM_7_1=y",
        'BR2_LINUX_KERNEL_CUSTOM_REPO_VERSION="cubie-linux-7.1"',
        "BR2_PACKAGE_BUSYBOX_SHOW_OTHERS=y",
        "BR2_PACKAGE_LINUX_TOOLS_GPIO=y",
    }
    assert required_config <= config

    fragment = _config_lines(
        repo_root / EXTERNAL_DIR / "board/zynq/linux-platform.fragment"
    )
    assert {"CONFIG_GPIOLIB=y", "CONFIG_GPIO_CDEV=y"} <= fragment


# @impl [SPEC-AC7020C-10] hw/zynq7000/alinx_ac7020c/SPECIFICATION.md
def test_alinx_ac7020c_root_filesystem_is_built_in(repo_root: Path) -> None:
    fragment = _config_lines(
        repo_root / EXTERNAL_DIR / "board/zynq/linux-platform.fragment"
    )
    assert {"CONFIG_EXT4_FS=y", "CONFIG_JBD2=y"} <= fragment


# @impl [SPEC-ZYNQ-PLATFORM-08] hw/zynq7000/SPECIFICATION.md
def test_all_zynq_configs_use_shared_realtime_fragment(repo_root: Path) -> None:
    configs = sorted((repo_root / CONFIG_DIR).glob("*_defconfig"))
    assert configs, "No Zynq Buildroot defconfigs found"
    fragment_setting = (
        'BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES='
        '"$(BR2_EXTERNAL_ABSTRACTX_ZYNQ7000_PATH)/board/zynq/linux-platform.fragment"'
    )
    for config in configs:
        config_lines = _config_lines(config)
        assert fragment_setting in config_lines, (
            f"{config.name} does not use the shared Zynq kernel fragment"
        )
        assert "BR2_PACKAGE_HOST_LINUX_HEADERS_CUSTOM_7_1=y" in config_lines, (
            f"{config.name} does not select Linux 7.1 headers"
        )

    fragment = _config_lines(
        repo_root / EXTERNAL_DIR / "board/zynq/linux-platform.fragment"
    )
    required_realtime = {
        "CONFIG_EXPERT=y",
        "CONFIG_PREEMPT=y",
        "CONFIG_PREEMPT_RT=y",
        "CONFIG_PREEMPT_RT_NEEDS_BH_LOCK=y",
        "CONFIG_IRQ_FORCED_THREADING=y",
        "CONFIG_HIGH_RES_TIMERS=y",
        "CONFIG_GPIOLIB=y",
        "CONFIG_GPIO_CDEV=y",
    }
    assert required_realtime <= fragment


# @impl [SPEC-ZYNQ-PLATFORM-09] hw/zynq7000/SPECIFICATION.md
def test_zynq_default_packet_storage_is_bram(repo_root: Path) -> None:
    bridge = (
        repo_root / "rtl" / "asp_axi_lite_bridge.sv"
    ).read_text(encoding="utf-8")
    assert "parameter integer FIFO_DEPTH         = 128" in bridge

    overlay = (
        repo_root
        / EXTERNAL_DIR
        / "board/zynq/overlays/xilinx/abstractx-uio.dtso"
    ).read_text(encoding="utf-8")
    assert 'abstractx,memory-mode = "bram";' in overlay
    assert "abstractx,ingress-capacity = <128>;" in overlay
    assert "abstractx,egress-capacity = <128>;" in overlay
    assert "dma-ring" not in overlay
    assert "reserved-memory" not in overlay


# @impl [SPEC-ZYNQ-PLATFORM-10] hw/zynq7000/SPECIFICATION.md
def test_all_zynq_configs_use_cpputest(repo_root: Path) -> None:
    configs = sorted((repo_root / CONFIG_DIR).glob("*_defconfig"))
    assert configs, "No Zynq Buildroot defconfigs found"
    for config in configs:
        lines = _config_lines(config)
        assert "BR2_PACKAGE_CPPUTEST=y" in lines
        assert "BR2_PACKAGE_CPPTEST=y" not in lines

    package_dir = repo_root / EXTERNAL_DIR / "package/cpputest"
    package_makefile = (package_dir / "cpputest.mk").read_text(encoding="utf-8")
    assert "CPPUTEST_VERSION = v4.0" in package_makefile
    assert "$(call github,cpputest,cpputest,$(CPPUTEST_VERSION))" in package_makefile


# @impl [SPEC-ZYNQ-PLATFORM-11] hw/zynq7000/SPECIFICATION.md
def test_all_zynq_configs_include_fpgautil(repo_root: Path) -> None:
    configs = sorted((repo_root / CONFIG_DIR).glob("*_defconfig"))
    assert configs, "No Zynq Buildroot defconfigs found"
    for config in configs:
        assert "BR2_PACKAGE_XILINX_FPGAUTIL=y" in _config_lines(config)
