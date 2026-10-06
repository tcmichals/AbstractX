#!/usr/bin/env bash
# ==============================================================================
# sync_kernel.sh
# Kernel sync helper across PCs for AbstractX QMTECH Zynq-7020
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
QMTECH_HW_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BLD_DIR="${QMTECH_HW_DIR}/bld"

ACTION="${1:-status}"

# Locate linux-cubie
LINUX_DIR=""
if [ -f "${BLD_DIR}/local.mk" ]; then
    LINUX_DIR="$(grep '^LINUX_OVERRIDE_SRCDIR' "${BLD_DIR}/local.mk" | cut -d'=' -f2 | xargs)"
fi

if [ -z "${LINUX_DIR}" ] || [ ! -d "${LINUX_DIR}/.git" ]; then
    echo "Error: Could not locate kernel source directory from ${BLD_DIR}/local.mk"
    exit 1
fi

case "${ACTION}" in
    status)
        echo "=== Kernel Status (${LINUX_DIR}) ==="
        git -C "${LINUX_DIR}" status -s
        echo "Branch: $(git -C "${LINUX_DIR}" branch --show-current)"
        echo "Latest: $(git -C "${LINUX_DIR}" log -1 --oneline)"
        ;;
    push)
        echo "=== Pushing Kernel Commits ==="
        git -C "${LINUX_DIR}" push origin "$(git -C "${LINUX_DIR}" branch --show-current)"
        ;;
    pull)
        echo "=== Pulling Kernel Commits ==="
        git -C "${LINUX_DIR}" pull --rebase origin "$(git -C "${LINUX_DIR}" branch --show-current)"
        ;;
    rebuild)
        echo "=== Forcing Kernel Rebuild in ${BLD_DIR} ==="
        make -C "${BLD_DIR}" linux-rebuild
        ;;
    *)
        echo "Usage: $0 {status|push|pull|rebuild}"
        exit 1
        ;;
esac
