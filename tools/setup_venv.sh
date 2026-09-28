#!/usr/bin/env bash
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
#
# AbstractX Python Virtual Environment Setup Helper
# Sets up an isolated .venv to comply with PEP 668 (externally-managed-environment).

set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV_DIR="${REPO_ROOT}/.venv"

echo "================================================================="
echo "  AbstractX Python Virtual Environment Setup (.venv)             "
echo "================================================================="

# 1. Check Python 3
if ! command -v python3 &>/dev/null; then
    echo "[ERROR] python3 not found. Please install Python 3.10+."
    exit 1
fi

# 2. Check python3-venv module
if ! python3 -m venv --help &>/dev/null; then
    echo "[ERROR] python3-venv is missing."
    echo "Install it with: sudo apt-get install python3-venv"
    exit 1
fi

# 3. Create .venv if not already present
if [ ! -d "${VENV_DIR}" ]; then
    echo "[1/3] Creating virtual environment at .venv..."
    python3 -m venv "${VENV_DIR}"
else
    echo "[1/3] Virtual environment already exists at .venv."
fi

# 4. Activate environment
echo "[2/3] Activating virtual environment..."
# shellcheck source=/dev/null
source "${VENV_DIR}/bin/activate"

# 5. Install dependencies
echo "[3/3] Installing dependencies from tools/visualizer/requirements.txt..."
pip install --upgrade pip --quiet
pip install -r "${REPO_ROOT}/tools/visualizer/requirements.txt"

# 6. Verification
python3 -c "
import imgui_bundle, numpy, pytest, yaml
print('\n[SUCCESS] Environment verified!')
print(f' - imgui-bundle version: {imgui_bundle.__version__}')
print(f' - numpy version       : {numpy.__version__}')
print(f' - pytest version      : {pytest.__version__}')
print(f' - yaml version        : {yaml.__version__}')
"

echo ""
echo "================================================================="
echo "  Setup Complete! To use the environment, run:                  "
echo "                                                                 "
echo "    source .venv/bin/activate                                    "
echo "                                                                 "
echo "  Then launch the visualizer:                                    "
echo "    python3 tools/visualizer/abstractx_studio.py --sim           "
echo "================================================================="
