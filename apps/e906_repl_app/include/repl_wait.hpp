/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstdint>

// @impl [SPEC-E906-REPL-16] apps/e906_repl_app/SPECIFICATION.md
// Run the VM hook (Ctrl-C, RPMsg), then sleep until any interrupt or `deadline` (mtime ticks).
// Never reads a peripheral register.
void repl_wait_step(uint64_t deadline) noexcept;
