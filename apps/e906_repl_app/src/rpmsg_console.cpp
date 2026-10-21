/*
 * rpmsg_console.cpp - VirtIO RPMsg Console Implementation for MicroPython REPL
 *
 * @impl [SPEC-E906-REPL-03] [SPEC-E906-REPL-05] [SPEC-E906-REPL-08] apps/e906_repl_app/SPECIFICATION.md
 */

#include "rpmsg_console.hpp"
#include <cstring>

namespace abstractx {

RpmsgConsole *g_repl_console = nullptr;

RpmsgConsole::RpmsgConsole(::hal::IRpmsg &rpmsg) noexcept
    : m_rpmsg(rpmsg) {
    g_repl_console = this;
}

void RpmsgConsole::init() noexcept {
    // @impl [SPEC-E906-REPL-05] apps/e906_repl_app/SPECIFICATION.md
    m_rpmsg.register_endpoint(REPL_ENDPOINT_ADDR, rx_callback, this);
    m_rpmsg.announce_service("abstractx-repl", REPL_ENDPOINT_ADDR);
}

void RpmsgConsole::rx_callback(const ::hal::RpmsgMessage &msg, void *user_data) noexcept {
    // @impl [SPEC-E906-REPL-05] apps/e906_repl_app/SPECIFICATION.md
    auto *self = static_cast<RpmsgConsole *>(user_data);
    if (!self) return;

    self->m_host_addr = msg.src;
    self->m_host_known = true;

    if (msg.data && msg.len > 0) {
        self->on_rx_data(msg.data, msg.len);
    }
}

void RpmsgConsole::on_rx_data(const uint8_t *data, size_t len) noexcept {
    // @impl [SPEC-E906-REPL-03] apps/e906_repl_app/SPECIFICATION.md
    for (size_t i = 0; i < len; ++i) {
        m_rx_channel.try_push(static_cast<char>(data[i]));
    }
}

void RpmsgConsole::write(const char *str, size_t len) noexcept {
    // @impl [SPEC-E906-REPL-05] apps/e906_repl_app/SPECIFICATION.md
    if (!str || len == 0) return;

    for (size_t i = 0; i < len; ++i) {
        stage_tx_char(str[i]);
        if (str[i] == '\n' || is_tx_buffer_full()) {
            flush();
        }
    }
}

bool RpmsgConsole::stage_tx_char(char ch) noexcept {
    if (m_out_len < OUTPUT_BUFFER_SIZE) {
        m_out_buf[m_out_len++] = ch;
        return true;
    }
    return false;
}

void RpmsgConsole::flush() noexcept {
    // @impl [SPEC-E906-REPL-05] apps/e906_repl_app/SPECIFICATION.md
    if (m_out_len == 0) return;

    if (m_host_known && m_rpmsg.is_driver_ready()) {
        ::hal::RpmsgMessage reply_msg{};
        reply_msg.src = REPL_ENDPOINT_ADDR;
        reply_msg.dst = m_host_addr;
        m_rpmsg.reply(reply_msg, m_out_buf, static_cast<uint16_t>(m_out_len));
    }
    m_out_len = 0;
}

} // namespace abstractx

#include "hal/trace.hpp"

extern "C" {
#include "py/mphal.h"

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    // @impl [SPEC-E906-REPL-05] apps/e906_repl_app/SPECIFICATION.md
    if (abstractx::g_repl_console) {
        abstractx::g_repl_console->write(str, len);
    }
    if (str) {
        for (size_t i = 0; i < len; ++i) {
            hal::Trace::putc(str[i]);
        }
    }
    return static_cast<mp_uint_t>(len);
}

} // extern "C"
