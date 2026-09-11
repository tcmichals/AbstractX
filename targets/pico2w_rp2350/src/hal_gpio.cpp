/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Raspberry Pi Pico 2 W (RP2350) GPIO Driver (IGpio)
 * -------------------------------------------------------------
 * 100% ISR and Hardware Driven. Zero busy loops.
 */

#include "abstractx_pico.hpp"
#include <array>

namespace abstractx::hal {

namespace {

struct PinCallbackEntry {
    uint32_t event_mask{0};
    GpioInterruptHandler handler{nullptr};
    void* context{nullptr};
};

static std::array<PinCallbackEntry, 48> g_pin_callbacks{};

#ifdef PICO_ON_DEVICE
static void raw_gpio_sdk_irq_callback(uint gpio, uint32_t events) {
    (void)events;
    if (gpio >= g_pin_callbacks.size()) return;
    auto& entry = g_pin_callbacks[gpio];
    if (entry.handler) {
        entry.handler(gpio, entry.context);
    }
}
#endif

} // namespace

void PicoGpio::configure_pin(uint32_t pin, PinMode mode, PinPull pull) {
#ifdef PICO_ON_DEVICE
    if (pin >= 48) return;

    gpio_init(pin);

    switch (mode) {
    case PinMode::Input:
        gpio_set_dir(pin, GPIO_IN);
        break;
    case PinMode::Output:
        gpio_set_dir(pin, GPIO_OUT);
        break;
    default:
        break;
    }

    switch (pull) {
    case PinPull::PullUp:
        gpio_pull_up(pin);
        break;
    case PinPull::PullDown:
        gpio_pull_down(pin);
        break;
    case PinPull::None:
    default:
        gpio_disable_pulls(pin);
        break;
    }
#else
    (void)pin; (void)mode; (void)pull;
#endif
}

void PicoGpio::write_pin(uint32_t pin, bool level) {
#ifdef PICO_ON_DEVICE
    if (pin >= 48) return;
    gpio_put(pin, level ? 1 : 0);
#else
    (void)pin; (void)level;
#endif
}

void PicoGpio::toggle_pin(uint32_t pin) {
#ifdef PICO_ON_DEVICE
    if (pin >= 48) return;
    gpio_xor_mask(1u << pin);
#else
    (void)pin;
#endif
}

bool PicoGpio::read_pin(uint32_t pin) {
#ifdef PICO_ON_DEVICE
    if (pin >= 48) return false;
    return gpio_get(pin);
#else
    (void)pin;
    return false;
#endif
}

bool PicoGpio::configure_interrupt(uint32_t pin, EdgeTrigger trigger, GpioInterruptHandler handler, void* context) {
    if (pin >= g_pin_callbacks.size()) return false;

    uint32_t mask = 0;
#ifdef PICO_ON_DEVICE
    if (trigger == EdgeTrigger::Rising || trigger == EdgeTrigger::Both) {
        mask |= GPIO_IRQ_EDGE_RISE;
    }
    if (trigger == EdgeTrigger::Falling || trigger == EdgeTrigger::Both) {
        mask |= GPIO_IRQ_EDGE_FALL;
    }

    g_pin_callbacks[pin].event_mask = mask;
    g_pin_callbacks[pin].handler = handler;
    g_pin_callbacks[pin].context = context;

    if (mask != 0) {
        gpio_set_irq_enabled_with_callback(pin, mask, true, &raw_gpio_sdk_irq_callback);
    } else {
        gpio_set_irq_enabled(pin, 0x0F, false);
    }
    return true;
#else
    (void)trigger; (void)handler; (void)context;
    return true;
#endif
}

void PicoGpio::enable_interrupt(uint32_t pin, bool enable) {
#ifdef PICO_ON_DEVICE
    if (pin >= g_pin_callbacks.size()) return;
    uint32_t mask = g_pin_callbacks[pin].event_mask;
    if (mask != 0) {
        gpio_set_irq_enabled(pin, mask, enable);
    }
#else
    (void)pin; (void)enable;
#endif
}

static PicoGpio g_pico_gpio;

PicoGpio& get_pico_gpio() noexcept {
    return g_pico_gpio;
}

IGpio& get_gpio_driver() noexcept {
    return g_pico_gpio;
}

IGpio& get_gpio() noexcept {
    return g_pico_gpio;
}

} // namespace abstractx::hal
