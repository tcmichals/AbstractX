#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Configure one AbstractX Zynq-7000 board in its dedicated Buildroot output tree.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZYNQ_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ABSTRACTX_ROOT="$(cd "${ZYNQ_DIR}/../.." && pwd)"
PROJECTS_ROOT="$(cd "${ABSTRACTX_ROOT}/.." && pwd)"
BR2_EXTERNAL="${ZYNQ_DIR}/buildroot_external"
BOARD="${1:-}"

BUILDROOT_DIR="${BUILDROOT_DIR:-${PROJECTS_ROOT}/buildroot}"
LINUX_DIR="${LINUX_DIR:-${PROJECTS_ROOT}/linux-cubie}"
UBOOT_DIR="${UBOOT_DIR:-${PROJECTS_ROOT}/u-boot-zynq}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        qmtech|qmtech-20|alinx|alinx-20|ac7020c|ac7010c|alinx-10) BOARD="$1"; shift ;;
        --buildroot|--linux|--uboot)
            [[ $# -ge 2 ]] || { echo "$1 requires a path" >&2; exit 2; }
            case "$1" in
                --buildroot) BUILDROOT_DIR="$2" ;;
                --linux) LINUX_DIR="$2" ;;
                --uboot) UBOOT_DIR="$2" ;;
            esac
            shift 2
            ;;
        --help|-h)
            echo "Usage: ./tools/setup_workspace.sh [qmtech-20|alinx-20|alinx-10] [--buildroot DIR] [--linux DIR] [--uboot DIR]"
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            echo "Usage: ./tools/setup_workspace.sh [qmtech-20|alinx-20|alinx-10] [--buildroot DIR] [--linux DIR] [--uboot DIR]" >&2
            exit 2
            ;;
    esac
done

ensure_repositories() {
    [[ -d "${BUILDROOT_DIR}/.git" ]] || git clone https://gitlab.com/buildroot.org/buildroot.git "${BUILDROOT_DIR}"
    [[ -d "${LINUX_DIR}/.git" ]] || git clone -b cubie-linux-7.1 git@github.com:tcmichals/linux-cubie.git "${LINUX_DIR}"
    [[ -d "${UBOOT_DIR}/.git" ]] || git clone git@github.com:tcmichals/u-boot-zynq.git "${UBOOT_DIR}"
}

configure_board() {
    local board_name="$1"
    local defconfig="$2"
    local workspace="$3"
    local output_dir="${ZYNQ_DIR}/${workspace}"

    mkdir -p "${output_dir}"
    cat > "${output_dir}/local.mk" <<EOF
LINUX_OVERRIDE_SRCDIR = ${LINUX_DIR}
UBOOT_OVERRIDE_SRCDIR = ${UBOOT_DIR}
EOF

    make -C "${BUILDROOT_DIR}" O="${output_dir}" \
        BR2_EXTERNAL="${BR2_EXTERNAL}" "${defconfig}"
    echo "Configured ${output_dir} (${board_name}; ${defconfig})"
}

ensure_repositories
case "${BOARD}" in
    qmtech|qmtech-20)
        configure_board qmtech abstractx_qmtech_zynq7020_defconfig bld.qmtech-20
        ;;
    alinx|alinx-20|ac7020c)
        configure_board alinx abstractx_alinx_ac7020c_defconfig bld.alinx-20
        ;;
    ac7010c|alinx-10)
        configure_board ac7010c abstractx_alinx_ac7010c_defconfig bld.alinx-10
        ;;
    *)
        echo "Usage: ./tools/setup_workspace.sh [qmtech-20|alinx-20|alinx-10]" >&2
        exit 2
        ;;
esac
