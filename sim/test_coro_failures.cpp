/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: coroutine failures without C++ exceptions [SPEC-ARCH-09]
 * An exception escaping a Task body must call the report hook and then trap
 * (checked in a forked child); when_all/when_any still complete through the
 * function-pointer completion hook.
 */
// @verifies [SPEC-ARCH-09] docs/DESIGN_SPECIFICATION.md#spec-arch-09

#include "asp_coro.hpp"

#include <cassert>
#include <csignal>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

using namespace abstractx::coro;

namespace {

int g_write_fd = -1;

void report_hook() noexcept {
    const char msg = 'R';
    (void)!write(g_write_fd, &msg, 1);
}

Task<int> throws_inside() {
    throw std::runtime_error("bug");
    co_return 1;
}

Task<int> value(int v) { co_return v; }
Task<void> nothing() { co_return; }

Task<int> combine() {
    auto [a, b] = co_await when_all(value(2), value(3));
    co_return a + b;
}

void test_unhandled_exception_reports_then_traps() {
    int fds[2];
    assert(pipe(fds) == 0);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        close(fds[0]);
        g_write_fd = fds[1];
        set_unhandled_exception_hook(report_hook);
        auto t = throws_inside();
        t.resume();
        _exit(0);  // not reached: the task must trap
    }
    close(fds[1]);
    char got = 0;
    ssize_t n = read(fds[0], &got, 1);
    int status = 0;
    waitpid(pid, &status, 0);
    assert(n == 1 && got == 'R' && "hook reports before the trap");
    assert(WIFSIGNALED(status) && "task traps instead of storing the exception");
    std::printf("[PASS] unhandled exception: hook called, child stopped by signal %d\n", WTERMSIG(status));
}

// Frame pool exhausted: the factory returns an empty Task. Awaiting it must report through
// the pool-exhausted hook and trap, never dereference the null handle (2026-10-28).
std::optional<Task<int>> g_empty;

Task<int> await_empty() {
    co_return co_await *g_empty;
}

void pool_hook() noexcept {
    const char msg = 'P';
    (void)!write(g_write_fd, &msg, 1);
}

void test_pool_exhaustion_reports_then_traps() {
    int fds[2];
    assert(pipe(fds) == 0);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        close(fds[0]);
        g_write_fd = fds[1];
        set_pool_exhausted_hook(pool_hook);
        auto waiter = await_empty();          // its frame exists before the pool runs out
        static std::optional<Task<int>> hog[4096];
        size_t n = 0;
        for (; n < 4096; ++n) {
            hog[n].emplace(value(1));
            if (!hog[n]->handle()) break;     // pool exhausted: the factory returned an empty Task
        }
        if (n == 4096) _exit(3);               // pool never ran out: test is wrong
        g_empty.emplace(std::move(*hog[n]));
        waiter.resume();
        _exit(0);  // not reached: awaiting the empty task must trap
    }
    close(fds[1]);
    char got = 0;
    ssize_t n = read(fds[0], &got, 1);
    int status = 0;
    waitpid(pid, &status, 0);
    assert(n == 1 && got == 'P' && "pool-exhausted hook reports before the trap");
    assert(WIFSIGNALED(status) && "awaiting an empty Task traps");
    std::printf("[PASS] pool exhausted: hook called, child stopped by signal %d\n", WTERMSIG(status));
}

void test_combinators_use_completion_hook() {
    auto t = combine();
    t.resume();
    assert(t.done() && t.get_result() == 5);
    auto n = nothing();
    n.resume();
    assert(n.done());
    std::printf("[PASS] when_all completes through CompletionHook\n");
}

}  // namespace

int main() {
    test_unhandled_exception_reports_then_traps();
    test_combinators_use_completion_hook();
    test_pool_exhaustion_reports_then_traps();
    return 0;
}
