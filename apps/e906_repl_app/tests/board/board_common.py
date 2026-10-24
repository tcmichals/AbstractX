#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""Skip support shared by board tests: pytest.skip under pytest, a plain exception otherwise."""
# @impl [SPEC-E906-REPL-15] apps/e906_repl_app/SPECIFICATION.md

try:
    import pytest
    Skipped = pytest.skip.Exception

    def skip(reason: str):
        pytest.skip(reason)
except ImportError:  # the board image has no pytest
    class Skipped(Exception):
        pass

    def skip(reason: str):
        raise Skipped(reason)
