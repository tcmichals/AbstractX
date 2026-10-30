// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

// When platform_idle_wait() must wake: the next status report, sooner while work is owed.
// @impl [SPEC-E906-IPC-02] [SPEC-E906-IRQ-05] targets/allwinner_e906/SPECIFICATION.md
namespace hal::idle {

inline constexpr uint64_t kOwedRetryUs = 50;

constexpr uint64_t wake_deadline_us(uint64_t now_us, uint64_t status_due_us, bool work_owed) noexcept {
    if (!work_owed) return status_due_us;
    const uint64_t retry = now_us + kOwedRetryUs;
    return retry < status_due_us ? retry : status_due_us;
}

} // namespace hal::idle
