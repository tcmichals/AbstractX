#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu

BOARD_DIR="$(dirname "$0")"
DTB="$(find "${BINARIES_DIR}" -maxdepth 1 -name 'zynq-*.dtb' -print -quit)"

install -m 0644 "${BOARD_DIR}/config.txt" "${BINARIES_DIR}/config.txt"

if [ -z "${DTB}" ]; then
    echo "No Zynq DTB found in ${BINARIES_DIR}" >&2
    exit 1
fi

cp "${DTB}" "${BINARIES_DIR}/system.dtb"
support/scripts/genimage.sh -c "${BOARD_DIR}/genimage.cfg"
