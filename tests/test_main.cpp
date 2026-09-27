/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX CppUTest SITL Master Test Runner
 */

#include "CppUTest/CommandLineTestRunner.h"

int main(int argc, char** argv) {
    return CommandLineTestRunner::RunAllTests(argc, argv);
}
