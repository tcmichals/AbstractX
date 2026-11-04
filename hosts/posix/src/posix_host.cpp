/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * @impl [SPEC-ARCH-16] docs/DESIGN_SPECIFICATION.md (POSIX host: eventfd + ppoll, timeout = deadline)
 */
#include "abstractx/host/posix_host.hpp"

#include <atomic>
#include <cerrno>
#include <ctime>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "abstractx/abstractx.hpp"

namespace abstractx::posix_host {
namespace {

// shared: created once and never closed, so a wake() racing the end of a run writes a valid fd
// (closing it let a late wake() write into a reused fd number: found by TSan, 2026-11-04)
std::atomic<int> s_efd{-1};
std::atomic<bool> s_stop{false};     // shared: stop() from any thread; run() reads
std::atomic<uint32_t> s_waits{0};    // shared: run() counts; anyone reads

void wake_hook() noexcept {
    const int fd = s_efd.load(std::memory_order_acquire);
    if (fd >= 0) {
        const uint64_t one = 1;
        (void)!::write(fd, &one, sizeof one);  // a counter: a wake before the wait is not lost
    }
}

uint64_t now_us() noexcept {
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);  // the clock LinuxTimerDriver::get_time_us() uses
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000u + static_cast<uint64_t>(ts.tv_nsec) / 1'000u;
}

// Block until a wake or the deadline (kNever: wake only), never longer than limit_us from now.
void wait_for(int fd, uint64_t deadline_us, uint64_t limit_us) noexcept {
    const uint64_t now = now_us();
    uint64_t until = deadline_us < limit_us ? deadline_us : limit_us;
    if (until <= now) return;
    const uint64_t d = until - now;
    timespec ts{static_cast<time_t>(d / 1'000'000u), static_cast<long>((d % 1'000'000u) * 1'000u)};
    pollfd p{fd, POLLIN, 0};
    s_waits.fetch_add(1, std::memory_order_relaxed);
    if (::ppoll(&p, 1, until == kNever ? nullptr : &ts, nullptr) > 0) {
        uint64_t v = 0;
        (void)!::read(fd, &v, sizeof v);  // clear the wake count
    }
}

int the_eventfd() noexcept {
    static const int fd = [] {
        const int f = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        s_efd.store(f, std::memory_order_release);
        return f;
    }();
    return fd;
}

bool loop(DoneFn done, void* ctx, uint64_t limit_us) noexcept {
    const int fd = the_eventfd();
    if (fd < 0) return false;
    set_wake({&wake_hook, &wake_hook});
    bool ok = false;
    for (;;) {
        const uint64_t next = step();
        if (done != nullptr && done(ctx)) { ok = true; break; }
        if (s_stop.exchange(false, std::memory_order_acq_rel)) break;
        if (limit_us != kNever && now_us() >= limit_us) break;
        if (next == kNow) continue;  // the OS preempts this thread for others; no spin to hide
        wait_for(fd, next, limit_us);
    }
    set_wake({});  // a wake() after this only increments the counter; the next run clears it
    return ok;
}

} // namespace

bool run_until(DoneFn done, void* ctx, uint64_t give_up_us) noexcept {
    return loop(done, ctx, now_us() + give_up_us);
}

void run() noexcept { (void)loop(nullptr, nullptr, kNever); }

void stop() noexcept {
    s_stop.store(true, std::memory_order_release);
    wake_hook();
}

uint32_t waits(bool reset) noexcept {
    return reset ? s_waits.exchange(0, std::memory_order_relaxed) : s_waits.load(std::memory_order_relaxed);
}

} // namespace abstractx::posix_host
