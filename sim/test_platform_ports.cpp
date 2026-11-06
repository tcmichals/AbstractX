// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
// @verifies [SPEC-LINUX-HAL-02] targets/linux/SPECIFICATION.md (get_spi_port get_i2c_port get_uart_port)
// Linux / host port list: each listed number returns its driver, any other number nullptr (not the
// weak core default, which answers only 0).
#include "abstractx/hal/platform.hpp"

#include <cstdio>

using namespace abstractx::hal;

static int g_fail = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);         \
            ++g_fail;                                                         \
        }                                                                     \
    } while (0)

int main() {
    CHECK(get_spi_port(0) == &get_spi_driver());
    CHECK(get_spi_port(1) == &get_spi_driver());     // SPI1 by its own number
    CHECK(get_i2c_port(0) == &get_i2c_driver());
    CHECK(get_i2c_port(1) == &get_i2c_driver());     // /dev/i2c-1 by its own number
    CHECK(get_uart_port(0) == &get_uart_driver());
    for (uint8_t n = 2; n < 8; ++n) {
        CHECK(get_spi_port(n) == nullptr);
        CHECK(get_i2c_port(n) == nullptr);
    }
    CHECK(get_uart_port(1) == nullptr && get_uart_port(3) == nullptr);
    std::printf("%s: %d failure(s)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
