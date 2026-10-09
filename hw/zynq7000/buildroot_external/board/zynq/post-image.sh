#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu

BOARD_DIR="$(dirname "$0")"
DTB="$(find "${BINARIES_DIR}" -maxdepth 1 -name 'zynq-*.dtb' -print -quit)"

if [ -z "${DTB}" ]; then
    echo "No Zynq DTB found in ${BINARIES_DIR}" >&2
    exit 1
fi

case "$(basename "${DTB}")" in
    zynq-qmtech-xc720.dtb)
        CONFIG="${BOARD_DIR}/config-qmtech.txt"
        ;;
    zynq-ac7010c.dtb)
        CONFIG="${BOARD_DIR}/config-alinx-ac7010c.txt"
        ;;
    zynq-ac7020c.dtb)
        CONFIG="${BOARD_DIR}/config-alinx-ac7020c.txt"
        # @impl [SPEC-AC7020C-09] hw/zynq7000/alinx_ac7020c/SPECIFICATION.md
        if "${HOST_DIR}/bin/fdtget" -p "${DTB}" /amba-pl/gpio@41200000 \
            >/dev/null 2>&1; then
            echo "AC7020C base DTB exposes PL GPIO without a loaded bitstream" >&2
            exit 1
        fi
        ;;
    *)
        echo "Unsupported Zynq DTB for overlay configuration: ${DTB}" >&2
        exit 1
        ;;
esac

install -m 0644 "${CONFIG}" "${BINARIES_DIR}/config.txt"
cp "${DTB}" "${BINARIES_DIR}/system.dtb"
support/scripts/genimage.sh -c "${BOARD_DIR}/genimage.cfg"
