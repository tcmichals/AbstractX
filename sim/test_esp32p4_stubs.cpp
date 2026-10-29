/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: ESP32-P4 HAL stubs refuse instead of reporting Ok [SPEC-ESP32P4-HAL-00]
 * The stub sources are compiled on the host (no ESP_PLATFORM). Each refusal completes through
 * the driver so the awaiting coroutine resumes with the error, driven like abstractx::step().
 */
// @verifies [SPEC-ESP32P4-HAL-00] targets/esp32p4/SPECIFICATION.md
#undef NDEBUG

#include "../targets/esp32p4/src/hal_mailbox.cpp"
#include "../targets/esp32p4/src/hal_spi.cpp"
#include "../targets/esp32p4/src/hal_timer.cpp"

#include "asp_coro.hpp"

#include <array>
#include <cassert>
#include <cstdio>

using namespace abstractx::hal;
using abstractx::Dispatcher;
using abstractx::coro::Task;

namespace {

void pump() {
    while (Dispatcher::has_pending_resumes()) Dispatcher::process();
}

Task<void> spi_once(Esp32p4Spi& spi, SpiResult& out) {
    std::array<uint8_t, 4> tx{1, 2, 3, 4}, rx{};
    out = co_await spi.transfer_async(tx, rx);
}

Task<void> doorbell_once(Esp32p4Mailbox& mb, MailboxResult& out) {
    out = co_await mb.notify_async(1, 0xABCD);
}

Task<void> sleep_once(Esp32p4Timer& t, TimerResult& out) {
    out = co_await t.sleep_ms_async(1);
}

} // namespace

int main() {
    Esp32p4Spi spi;
    assert(!spi.init(SpiConfig{}));
    SpiResult sr{};
    sr.status = SpiStatus::Ok;
    auto ts = spi_once(spi, sr);
    ts.resume();
    pump();
    assert(ts.done() && sr.status == SpiStatus::NotSupported && sr.transferred_bytes == 0);

    Esp32p4Mailbox mb;
    assert(!mb.send_doorbell(1, 2));
    MailboxResult mr{};
    auto tm = doorbell_once(mb, mr);
    tm.resume();
    pump();
    assert(tm.done() && mr.status == MailboxStatus::NotSupported);

    Esp32p4Timer timer;
    TimerResult tr{};
    auto tt = sleep_once(timer, tr);
    tt.resume();
    pump();
    assert(tt.done() && tr.status == TimerStatus::Error);

    assert(Dispatcher::queue_depth() == 0);
    std::printf("[PASS] ESP32-P4 stubs: SPI NotSupported, mailbox NotSupported, sleep Error\n");
    return 0;
}
