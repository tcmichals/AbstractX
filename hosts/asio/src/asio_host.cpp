/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * @impl [SPEC-ARCH-15] docs/DESIGN_SPECIFICATION.md (Boost.Asio host: pump handler, steady_timer, post)
 */
#include "abstractx/host/asio_host.hpp"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>

#include <atomic>
#include <chrono>
#include <ctime>
#include <optional>

#include "abstractx/abstractx.hpp"

namespace abstractx::asio_host {
namespace {

namespace asio = boost::asio;

// shared: attach/detach (owner thread) write, wake_hook (any thread) reads; atomic
std::atomic<asio::io_context*> s_ioc{nullptr};
std::atomic<bool> s_post_pending{false};   // shared: wake_hook sets, pump clears; one post in flight
std::atomic<uint32_t> s_waits{0};          // shared: pump counts, anyone reads
std::atomic<bool> s_stop{false};           // shared: stop() from any thread, pump reads
std::optional<asio::steady_timer> s_timer; // owner thread only (the io_context's thread)
// Keeps run() alive while attached: a wake sets s_post_pending before its asio::post lands, and in that
// gap the io_context had no work and run() returned (lost wake; conformance check 4 under load).
std::optional<asio::executor_work_guard<asio::io_context::executor_type>> s_guard;   // owner thread only
DoneFn s_done = nullptr;                   // owner thread only
void* s_ctx = nullptr;
uint64_t s_limit_us = kNever;
bool s_ok = false;

uint64_t now_us() noexcept {
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);  // the clock LinuxTimerDriver::get_time_us() uses
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000u + static_cast<uint64_t>(ts.tv_nsec) / 1'000u;
}

void pump() noexcept;

void post_pump(asio::io_context& ioc) noexcept {
    if (!s_post_pending.exchange(true, std::memory_order_acq_rel)) asio::post(ioc, &pump);
}

void wake_hook() noexcept {
    asio::io_context* ioc = s_ioc.load(std::memory_order_acquire);
    if (ioc != nullptr) post_pump(*ioc);   // io_context::post is thread-safe
}

// One AbstractX pass; the host's whole loop is this handler re-armed by post, timer or wake.
void pump() noexcept {
    asio::io_context* ioc = s_ioc.load(std::memory_order_acquire);
    if (ioc == nullptr) return;
    s_post_pending.store(false, std::memory_order_release);   // a wake during step() posts again
    const uint64_t next = step();
    if (s_done != nullptr && s_done(s_ctx)) {
        s_ok = true;
        ioc->stop();
        return;
    }
    const uint64_t now = now_us();
    if (s_stop.exchange(false, std::memory_order_acq_rel) || (s_limit_us != kNever && now >= s_limit_us)) {
        ioc->stop();
        return;
    }
    if (next == kNow) {                    // ready work: go round through the queue (other handlers run)
        post_pump(*ioc);
        return;
    }
    s_waits.fetch_add(1, std::memory_order_relaxed);
    uint64_t until = next;
    if (s_limit_us != kNever && (until == kNever || until > s_limit_us)) until = s_limit_us;
    if (until == kNever) {
        s_timer->cancel();                 // idle: only a wake (post) runs us; the work guard keeps run() alive
        return;
    }
    s_timer->expires_after(std::chrono::microseconds(until > now ? until - now : 0));
    s_timer->async_wait([](const boost::system::error_code& ec) {
        if (!ec) pump();                   // operation_aborted: re-armed or cancelled meanwhile
    });
}

} // namespace

void attach(asio::io_context& ioc) noexcept {
    s_timer.emplace(ioc);
    s_guard.emplace(ioc.get_executor());
    s_post_pending.store(false, std::memory_order_relaxed);
    s_ioc.store(&ioc, std::memory_order_release);
    set_wake({&wake_hook, &wake_hook});
    post_pump(ioc);
}

void detach() noexcept {
    set_wake({});
    s_ioc.store(nullptr, std::memory_order_release);
    if (s_timer) s_timer->cancel();
    s_timer.reset();
    s_guard.reset();
}

namespace {
bool own_loop(DoneFn done, void* ctx, uint64_t limit_us) noexcept {
    asio::io_context ioc;
    s_done = done;
    s_ctx = ctx;
    s_limit_us = limit_us;
    s_ok = false;
    attach(ioc);
    ioc.run();
    detach();
    s_done = nullptr;
    s_ctx = nullptr;
    s_limit_us = kNever;
    return s_ok;
}
} // namespace

// @impl [SPEC-ARCH-15] docs/DESIGN_SPECIFICATION.md (Boost.Asio host: own io_context until done)
bool run_until(DoneFn done, void* ctx, uint64_t give_up_us) noexcept {
    return own_loop(done, ctx, now_us() + give_up_us);
}

void run() noexcept { (void)own_loop(nullptr, nullptr, kNever); }

void stop() noexcept {
    s_stop.store(true, std::memory_order_release);
    wake_hook();
}

uint32_t waits(bool reset) noexcept {
    return reset ? s_waits.exchange(0, std::memory_order_relaxed) : s_waits.load(std::memory_order_relaxed);
}

} // namespace abstractx::asio_host
