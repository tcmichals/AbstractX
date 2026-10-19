// @impl [SPEC-E906-IPC-02] targets/allwinner_e906/SPECIFICATION.md
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Host test double for the E906 message box: records kicks instead of
 * touching MMIO, so targets/allwinner_e906/src/rpmsg.cpp runs on the host.
 */

#include "hal/msgbox.hpp"
#include <functional>

namespace hal {

int g_fake_msgbox_sends[4] = {0, 0, 0, 0};
int g_fake_msgbox_full_sends_left = 0;
int g_fake_msgbox_rx_pending = 0;
std::function<void()> g_fake_msgbox_on_send = nullptr;

void MsgBox::init(bool) noexcept {}

bool MsgBox::send(Channel ch, uint32_t) noexcept {
    if (g_fake_msgbox_full_sends_left > 0) {
        --g_fake_msgbox_full_sends_left;
        return false;
    }
    ++g_fake_msgbox_sends[static_cast<int>(ch) & 3];
    if (g_fake_msgbox_on_send) {
        g_fake_msgbox_on_send();
    }
    return true;
}

std::optional<uint32_t> MsgBox::receive(Channel) noexcept {
    if (g_fake_msgbox_rx_pending > 0) {
        --g_fake_msgbox_rx_pending;
        return 0;
    }
    return std::nullopt;
}

bool MsgBox::is_rx_pending(Channel) noexcept {
    return g_fake_msgbox_rx_pending > 0;
}

void MsgBox::enable_rx_irq(Channel, bool) noexcept {}
void MsgBox::clear_irq_status(Channel) noexcept {}

} // namespace hal
