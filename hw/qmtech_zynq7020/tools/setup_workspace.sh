#!/usr/bin/env bash
# ==============================================================================
# setup_workspace.sh
# Multi-PC Workspace Setup and Buildroot Environment for QMTECH Zynq-7020
# Follows the same zero-patch, local.mk override pattern as cubie-a5e
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
QMTECH_HW_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ABSTRACTX_ROOT="$(cd "${QMTECH_HW_DIR}/../.." && pwd)"
PROJECTS_ROOT="$(cd "${ABSTRACTX_ROOT}/.." && pwd)"

# Repository URLs
REPO_QMTECH="git@github.com:tcmichals/QMTECH.git"
REPO_UBOOT_ZYNQ="git@github.com:tcmichals/u-boot-zynq.git"
REPO_LINUX_CUBIE="git@github.com:tcmichals/linux-cubie.git"
REPO_BUILDROOT="https://gitlab.com/buildroot.org/buildroot.git"
KERNEL_BRANCH="cubie-linux-7.1"

echo "======================================================================"
echo " AbstractX QMTECH Zynq-7020 Workspace Setup"
echo " AbstractX Root: ${ABSTRACTX_ROOT}"
echo " Projects Root:  ${PROJECTS_ROOT}"
echo " Build Dir:      ${QMTECH_HW_DIR}/bld"
echo "======================================================================"

cd "${PROJECTS_ROOT}"

# 1. Locate or Clone buildroot
echo ""
echo "[1/4] Checking Buildroot..."
BUILDROOT_DIR=""
if [ -d "${PROJECTS_ROOT}/cubie/buildroot/.git" ]; then
    BUILDROOT_DIR="${PROJECTS_ROOT}/cubie/buildroot"
    echo "  -> Found shared Buildroot at: ${BUILDROOT_DIR}"
elif [ -d "${PROJECTS_ROOT}/buildroot/.git" ]; then
    BUILDROOT_DIR="${PROJECTS_ROOT}/buildroot"
    echo "  -> Found Buildroot at: ${BUILDROOT_DIR}"
else
    BUILDROOT_DIR="${PROJECTS_ROOT}/buildroot"
    echo "  -> Cloning upstream Buildroot into ${BUILDROOT_DIR}..."
    git clone "${REPO_BUILDROOT}" "${BUILDROOT_DIR}"
fi

# 2. Locate or Clone linux-cubie
echo ""
echo "[2/4] Checking linux-cubie repository..."
LINUX_DIR=""
if [ -d "${PROJECTS_ROOT}/cubie/linux-cubie/.git" ]; then
    LINUX_DIR="${PROJECTS_ROOT}/cubie/linux-cubie"
    echo "  -> Found shared linux-cubie at: ${LINUX_DIR}"
elif [ -d "${PROJECTS_ROOT}/linux-cubie/.git" ]; then
    LINUX_DIR="${PROJECTS_ROOT}/linux-cubie"
    echo "  -> Found linux-cubie at: ${LINUX_DIR}"
else
    LINUX_DIR="${PROJECTS_ROOT}/linux-cubie"
    echo "  -> Cloning linux-cubie (${KERNEL_BRANCH}) into ${LINUX_DIR}..."
    git clone -b "${KERNEL_BRANCH}" "${REPO_LINUX_CUBIE}" "${LINUX_DIR}"
fi

# 3. Locate or Clone u-boot-zynq
echo ""
echo "[3/4] Checking u-boot-zynq repository..."
UBOOT_DIR=""
if [ -d "${PROJECTS_ROOT}/u-boot-zynq/.git" ]; then
    UBOOT_DIR="${PROJECTS_ROOT}/u-boot-zynq"
    echo "  -> Found u-boot-zynq at: ${UBOOT_DIR}"
else
    UBOOT_DIR="${PROJECTS_ROOT}/u-boot-zynq"
    echo "  -> Cloning u-boot-zynq into ${UBOOT_DIR}..."
    git clone "${REPO_UBOOT_ZYNQ}" "${UBOOT_DIR}"
fi

# 4. Locate or Clone QMTECH external tree
echo ""
echo "[4/4] Checking QMTECH external tree..."
QMTECH_DIR=""
if [ -d "${PROJECTS_ROOT}/QMTECH/.git" ]; then
    QMTECH_DIR="${PROJECTS_ROOT}/QMTECH"
    echo "  -> Found QMTECH tree at: ${QMTECH_DIR}"
else
    QMTECH_DIR="${PROJECTS_ROOT}/QMTECH"
    echo "  -> Cloning QMTECH tree into ${QMTECH_DIR}..."
    git clone "${REPO_QMTECH}" "${QMTECH_DIR}"
fi

# 5. Configure hw/qmtech_zynq7020/bld
echo ""
echo "Configuring out-of-tree Buildroot environment in ${QMTECH_HW_DIR}/bld..."
mkdir -p "${QMTECH_HW_DIR}/bld"

cat << EOF > "${QMTECH_HW_DIR}/bld/local.mk"
# Local Buildroot package overrides for live development
LINUX_OVERRIDE_SRCDIR = ${LINUX_DIR}
UBOOT_OVERRIDE_SRCDIR = ${UBOOT_DIR}
EOF
echo "  -> Created ${QMTECH_HW_DIR}/bld/local.mk"

if [ ! -f "${QMTECH_HW_DIR}/bld/.config" ]; then
    echo "  -> Initializing ${QMTECH_HW_DIR}/bld with zynq_qmtech_xc720_defconfig..."
    make -C "${BUILDROOT_DIR}" \
         O="${QMTECH_HW_DIR}/bld" \
         BR2_EXTERNAL="${QMTECH_DIR}/zynq_qmtech_xc720" \
         zynq_qmtech_xc720_defconfig
else
    echo "  -> ${QMTECH_HW_DIR}/bld/.config exists. Updated local.mk."
fi

echo ""
echo "======================================================================"
echo " Workspace Setup Complete!"
echo " Output directory: ${QMTECH_HW_DIR}/bld"
echo " Build commands:"
echo "   Full Build:       make -C ${QMTECH_HW_DIR}/bld"
echo "   Kernel Rebuild:   make -C ${QMTECH_HW_DIR}/bld linux-rebuild"
echo "   U-Boot Rebuild:   make -C ${QMTECH_HW_DIR}/bld uboot-rebuild"
echo "======================================================================"
