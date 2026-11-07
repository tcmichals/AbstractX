// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
// @verifies [SPEC-E906-BENCH-09] apps/e906_bench/SPECIFICATION.md (StepProfile; tests/test_run_e906_bench_suite.py checks its wiring)
#include "CppUTest/TestHarness.h"

#include "abstractx/trace/step_profile.hpp"

using abstractx::stats::StepPhase;
using abstractx::stats::StepProfile;

TEST_GROUP(StepProfile) {};

TEST(StepProfile, EachPhaseGetsTheTimeSinceThePreviousMark) {
    StepProfile p;
    p.begin(100);
    p.mark(StepPhase::Timer, 110);
    p.mark(StepPhase::Dispatch, 130);
    p.mark(StepPhase::Deadline, 160);
    UNSIGNED_LONGLONGS_EQUAL(10, p.total(StepPhase::Timer));
    UNSIGNED_LONGLONGS_EQUAL(20, p.total(StepPhase::Dispatch));
    UNSIGNED_LONGLONGS_EQUAL(30, p.total(StepPhase::Deadline));
    UNSIGNED_LONGLONGS_EQUAL(0, p.total(StepPhase::Io));
    LONGS_EQUAL(1, p.passes);
}

TEST(StepProfile, TotalsAddUpOverPassesPastThe32BitLimit) {
    StepProfile p;
    for (uint32_t i = 0; i < 3; ++i) {
        p.begin(1000 * i);
        p.mark(StepPhase::Io, 1000 * i + 7);
    }
    UNSIGNED_LONGLONGS_EQUAL(21, p.total(StepPhase::Io));
    LONGS_EQUAL(3, p.passes);
    // mcycle wraps at 21 s: one phase across the wrap is still its true length
    p.reset();
    p.begin(0xFFFFFFF0u);
    p.mark(StepPhase::Io, 0x00000010u);
    UNSIGNED_LONGLONGS_EQUAL(0x20, p.total(StepPhase::Io));
}

// The bench resets from a coroutine, inside Dispatch: the mark that ends that phase must stay small.
TEST(StepProfile, ResetInsideAPhaseKeepsTheNextMarkSmall) {
    StepProfile p;
    p.begin(5000);
    p.reset();
    p.mark(StepPhase::Dispatch, 5040);
    UNSIGNED_LONGLONGS_EQUAL(40, p.total(StepPhase::Dispatch));
    LONGS_EQUAL(0, p.passes);
}

#if !ABX_STEP_PROFILE
// Timing builds carry no stamps.
TEST(StepProfile, OffTheStampsAreEmpty) {
    abstractx::stats::step_begin();
    abstractx::stats::step_mark(StepPhase::Timer);
    CHECK_FALSE(ABX_STEP_PROFILE);
}
#endif
