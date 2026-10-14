/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Host test double for the E906 message box: records kicks instead of
 * touching MMIO, so targets/allwinner_e906/src/rpmsg.cpp runs on the host.
 */

#include "hal/msgbox.hpp"

namespace hal {

int g_fake_msgbox_sends[4] = {0, 0, 0, 0};

void MsgBox::init(bool) noexcept {}
bool MsgBox::send(Channel ch, uint32_t) noexcept {
    ++g_fake_msgbox_sends[static_cast<int>(ch) & 3];
    return true;
}
std::optional<uint32_t> MsgBox::receive(Channel) noexcept { return std::nullopt; }
bool MsgBox::is_rx_pending(Channel) noexcept { return false; }
void MsgBox::enable_rx_irq(Channel, bool) noexcept {}
void MsgBox::clear_irq_status(Channel) noexcept {}

} // namespace hal
