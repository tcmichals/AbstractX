// @verifies [SPEC-ARCH-17] docs/DESIGN_SPECIFICATION.md
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A client that declares its own abstractx::Task builds next to AbstractX when it defines
 * ABSTRACTX_NO_TASK_ALIASES; AbstractX's coro::Task still works.
 */

#define ABSTRACTX_NO_TASK_ALIASES 1

// The client's own declarations come first, then AbstractX: include order must not matter.
namespace abstractx {
template <typename T = void> class Task { public: int client_marker = 42; };
template <typename... Ts> constexpr int when_all(Ts&&...) noexcept { return 1; }
template <typename... Ts> constexpr int when_any(Ts&&...) noexcept { return 2; }
} // namespace abstractx

#include "abstractx/coro.hpp"
#include "abstractx/hal/platform.hpp"

#include "CppUTest/TestHarness.h"

namespace {
abstractx::coro::Task<int> answer() { co_return 7; }

abstractx::coro::Task<void> outer(int& out) {
    co_await abstractx::yield();
    out = co_await answer();
}
} // namespace

TEST_GROUP(CoroNoTaskAliases) {
    void teardown() override {
        while (abstractx::Dispatcher::has_pending_resumes()) {
            abstractx::Dispatcher::process();
        }
    }
};

TEST(CoroNoTaskAliases, ClientTaskIsTheClientsOwn) {
    abstractx::Task<> client;
    LONGS_EQUAL(42, client.client_marker);
    LONGS_EQUAL(1, abstractx::when_all());
    LONGS_EQUAL(2, abstractx::when_any());
}

TEST(CoroNoTaskAliases, CoroTaskStillRunsThroughDispatcher) {
    int out = 0;
    auto t = outer(out);
    t.resume(); // starts the task once; the dispatcher does the rest
    for (int i = 0; i < 16 && abstractx::Dispatcher::has_pending_resumes(); ++i) {
        abstractx::Dispatcher::process();
    }
    LONGS_EQUAL(7, out);
}
