#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-AC7020C-06] hw/zynq7000/alinx_ac7020c/SPECIFICATION.md
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
XSA="${1:-${TARGET_DIR}/output/ac7020c_base.xsa}"

find_uboot() {
    local dir="${TARGET_DIR}"
    while [[ "${dir}" != "/" ]]; do
        if [[ -d "${dir}/u-boot-zynq/.git" ]]; then
            printf '%s\n' "${dir}/u-boot-zynq"
            return 0
        fi
        dir="$(dirname "${dir}")"
    done
    return 1
}

UBOOT_DIR="${2:-}"
if [[ -z "${UBOOT_DIR}" ]]; then
    UBOOT_DIR="$(find_uboot)" || {
        echo "Unable to locate sibling u-boot-zynq; pass its path as argument 2." >&2
        exit 1
    }
fi

if [[ ! -f "${XSA}" ]]; then
    echo "XSA not found: ${XSA}" >&2
    exit 1
fi
if [[ ! -d "${UBOOT_DIR}/board/xilinx/zynq" ]]; then
    echo "Not a U-Boot Zynq tree: ${UBOOT_DIR}" >&2
    exit 1
fi
DEST_DIR="${UBOOT_DIR}/board/xilinx/zynq/zynq-ac7020c"
mkdir -p "${DEST_DIR}"
for member in ps7_init_gpl.c ps7_init_gpl.h; do
    if ! unzip -l "${XSA}" | grep -q "${member}"; then
        echo "XSA does not contain ${member}: ${XSA}" >&2
        exit 1
    fi
    unzip -p "${XSA}" "${member}" > "${DEST_DIR}/${member}"
    [[ -s "${DEST_DIR}/${member}" ]]
done

sed -i -E \
    's/(ps7GetSiliconVersion|ps7_init|ps7_post_config|ps7_debug|perf_reset_and_start_timer)[[:space:]]*\(\)/\1(void)/g' \
    "${DEST_DIR}/ps7_init_gpl.c" "${DEST_DIR}/ps7_init_gpl.h"
sed -i -E \
    's/(ps7GetSiliconVersion|perf_reset_and_start_timer)\(void\);/\1();/g' \
    "${DEST_DIR}/ps7_init_gpl.c"

echo "Installed ${DEST_DIR}/ps7_init_gpl.c and ps7_init_gpl.h"
