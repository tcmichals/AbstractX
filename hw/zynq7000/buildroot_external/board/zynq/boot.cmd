# SPDX-License-Identifier: GPL-2.0-or-later
# AbstractX Zynq boot script: merge base DTB and selected overlays before Linux.

setenv bootargs "console=ttyPS0,115200 root=/dev/mmcblk0p2 rootwait rw"
setenv scriptaddr 0x10000000
setenv kernel_addr_r 0x01000000
setenv fdt_addr_r 0x10000000
setenv overlay_addr_r 0x11000000
setenv config_addr_r 0x12000000

if load mmc 0:1 ${config_addr_r} config.txt; then
    env import -t ${config_addr_r} ${filesize}
fi

if test -n "${cmdline}"; then
    setenv bootargs "${bootargs} ${cmdline}"
fi

if load mmc 0:1 ${fdt_addr_r} system.dtb; then
    fdt addr ${fdt_addr_r}
    fdt resize 0x10000
else
    echo "ERROR: system.dtb not found"
    reset
fi

if test -n "${dtoverlay}"; then
    for overlay in ${dtoverlay}; do
        if load mmc 0:1 ${overlay_addr_r} ${overlay}.dtbo; then
            if fdt apply ${overlay_addr_r}; then
                echo "Applied ${overlay}.dtbo"
            else
                echo "ERROR: failed to apply ${overlay}.dtbo"
                reset
            fi
        else
            echo "ERROR: overlay ${overlay}.dtbo not found"
            reset
        fi
    done
fi

if load mmc 0:1 ${kernel_addr_r} uImage; then
    bootm ${kernel_addr_r} - ${fdt_addr_r}
else
    echo "ERROR: uImage not found"
    reset
fi
