#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Configure one AbstractX Zynq-7000 board in the shared bld.zynq output tree.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZYNQ_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ABSTRACTX_ROOT="$(cd "${ZYNQ_DIR}/../.." && pwd)"
PROJECTS_ROOT="$(cd "${ABSTRACTX_ROOT}/.." && pwd)"
BR2_EXTERNAL="${ZYNQ_DIR}/buildroot_external"
BOARD="${1:-}"

BUILDROOT_DIR="${PROJECTS_ROOT}/buildroot"
LINUX_DIR="${PROJECTS_ROOT}/linux-cubie"
UBOOT_DIR="${PROJECTS_ROOT}/u-boot-zynq"

ensure_repositories() {
    [[ -d "${BUILDROOT_DIR}/.git" ]] || git clone https://gitlab.com/buildroot.org/buildroot.git "${BUILDROOT_DIR}"
    [[ -d "${LINUX_DIR}/.git" ]] || git clone -b cubie-linux-7.1 git@github.com:tcmichals/linux-cubie.git "${LINUX_DIR}"
    [[ -d "${UBOOT_DIR}/.git" ]] || git clone git@github.com:tcmichals/u-boot-zynq.git "${UBOOT_DIR}"
}

configure_board() {
    local board_name="$1"
    local defconfig="$2"
    local output_dir="${ZYNQ_DIR}/bld.zynq"
    local marker="${output_dir}/.abstractx-board"

    if [[ -f "${marker}" ]] && [[ "$(cat "${marker}")" != "${board_name}" ]]; then
        echo "Switching bld.zynq from $(cat "${marker}") to ${board_name}; clearing generated output."
        find "${output_dir}" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
    fi

    mkdir -p "${output_dir}"
    cat > "${output_dir}/local.mk" <<EOF
LINUX_OVERRIDE_SRCDIR = ${LINUX_DIR}
UBOOT_OVERRIDE_SRCDIR = ${UBOOT_DIR}
EOF

    make -C "${BUILDROOT_DIR}" O="${output_dir}" \
        BR2_EXTERNAL="${BR2_EXTERNAL}" "${defconfig}"
    printf '%s\n' "${board_name}" > "${marker}"
    echo "Configured ${output_dir} (${defconfig})"
}

ensure_repositories
case "${BOARD}" in
    qmtech)
        configure_board qmtech abstractx_qmtech_zynq7020_defconfig
        ;;
    alinx|ac7020c)
        configure_board alinx abstractx_alinx_ac7020c_defconfig
        ;;
    *)
        echo "Usage: $0 [qmtech|alinx]" >&2
        exit 2
        ;;
esac
