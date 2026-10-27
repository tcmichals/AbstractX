/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Universal Magnetometer / Digital Compass Driver (HMC5883L & QMC5883L)
 */
// @impl [SPEC-MAG-01] docs/DESIGN_SPECIFICATION.md

#ifndef APPLIB_DRIVERS_MAG_COMPASS_HPP
#define APPLIB_DRIVERS_MAG_COMPASS_HPP

#include "qmc5883l.hpp"
#include "hmc5883l.hpp"

namespace applib::drivers::mag {

class UniversalCompass : public IMagDriver {
public:
    explicit UniversalCompass(hal::II2c& i2c_driver)
        : i2c_(i2c_driver), hmc_(i2c_driver), qmc_(i2c_driver) {}

    coro::Task<bool> init_async() override {
        // Try HMC5883L first (addr 0x1E, whoami "H43")
        if (co_await hmc_.init_async()) {
            active_chip_ = ChipType::Hmc;
            co_return true;
        }

        // Try QMC5883L next (addr 0x0D)
        if (co_await qmc_.init_async()) {
            active_chip_ = ChipType::Qmc;
            co_return true;
        }

        co_return false;
    }

    coro::Task<MagSample> read_sample_async() override {
        if (active_chip_ == ChipType::Hmc) {
            co_return co_await hmc_.read_sample_async();
        } else if (active_chip_ == ChipType::Qmc) {
            co_return co_await qmc_.read_sample_async();
        }
        co_return MagSample{};
    }

    static Tlp64 to_tlp(const MagSample& s, uint32_t seq = 0) noexcept {
        return Hmc5883l::to_tlp(s, seq);
    }

private:
    enum class ChipType : uint8_t { None = 0, Hmc = 1, Qmc = 2 };
    hal::II2c& i2c_;
    Hmc5883l   hmc_;
    Qmc5883l   qmc_;
    ChipType   active_chip_{ChipType::None};
};

using Compass = UniversalCompass;

} // namespace applib::drivers::mag

namespace abstractx::drivers::mag {
    using applib::drivers::mag::Compass;
    using applib::drivers::mag::UniversalCompass;
} // namespace abstractx::drivers::mag

#endif // APPLIB_DRIVERS_MAG_COMPASS_HPP
