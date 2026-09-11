/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX ESP32-P4 HAL Mailbox / IPC Implementation
 */

#include "abstractx/hal/mailbox.hpp"

namespace abstractx::hal {

class Esp32p4Mailbox : public IMailbox {
public:
    void init() override {}

    bool send_doorbell(uint32_t channel, uint32_t message) override {
        (void)channel;
        (void)message;
        return true;
    }

    void register_receiver(uint32_t channel, MailboxCallback callback, void* context) override {
        (void)channel;
        (void)callback;
        (void)context;
    }

    void enable_channel(uint32_t channel, bool enable) override {
        (void)channel;
        (void)enable;
    }

protected:
    void start_hardware_transfer_from_isr(const MailboxTxRequest& req) noexcept override {
        send_doorbell(req.channel, req.message);
        MailboxResult result{};
        result.status = MailboxStatus::Ok;
        result.channel = req.channel;
        result.message = req.message;
        push_completion_from_isr(req, result);
        set_hardware_idle_from_isr();
    }
};

} // namespace abstractx::hal
