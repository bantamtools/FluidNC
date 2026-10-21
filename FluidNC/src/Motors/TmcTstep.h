// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.
//
// TSTEP-domain thresholds for Trinamic velocity registers (TPWMTHRS, TCOOLTHRS).
// Pure, so it is natively testable.
//
// The chip measures TSTEP as the time between 1/256 microsteps, in clock cycles.
// A threshold register holds the TSTEP at the switch-over speed: the slower the
// speed, the larger the value. TPWMTHRS: StealthChop is active while
// TSTEP >= TPWMTHRS, SpreadCycle above that speed.
#pragma once

#include <cstdint>

namespace MotorDrivers {
    namespace TmcTstep {
        constexpr uint32_t kMax = 0xFFFFF;  // the threshold registers are 20 bits

        // The TPWMTHRS that CoolStep run mode has always programmed on the TMC2209:
        // a fixed 5 mm/min with a hard-coded 80 steps/mm and no clock term, which is
        // 150000 (0x249F0) on every axis. On the HandWriter Z (28.57 steps/mm,
        // 8 microsteps, 12 MHz) that is a switch-over of about 5 mm/min. Kept
        // bit-identical so configs that do not set tpwmthrs_mm_per_min keep the
        // register value they were validated with (internal tracker).
        inline uint32_t legacyCoolStepTpwmthrs() {
            uint32_t fr = 5;
            return static_cast<uint32_t>(1000000 / ((fr / 60.0) * 80));
        }

        // TSTEP at `mm_per_min` for an axis with `steps_per_mm` (in microsteps at
        // `microsteps` resolution), with chip clock `fclk` Hz (the driver passes
        // TrinamicBase::fclk, 12.7 MHz; the datasheet nominal is 12 MHz, so the real
        // switch-over is ~5% below a value computed at 12 MHz). Saturates at kMax.
        // A non-positive speed has no TSTEP and returns kMax (slowest possible).
        inline uint32_t forSpeed(double mm_per_min, double steps_per_mm, int microsteps, double fclk) {
            if (!(mm_per_min > 0) || !(steps_per_mm > 0) || microsteps <= 0) {
                return kMax;
            }
            double per256 = mm_per_min / 60.0 * steps_per_mm * (256.0 / microsteps);
            double tstep  = fclk / per256;
            if (!(tstep < double(kMax))) {
                return kMax;
            }
            // Never 0: TPWMTHRS 0 means StealthChop at every speed, the opposite of a
            // very fast threshold.
            return tstep < 1.0 ? 1u : static_cast<uint32_t>(tstep);
        }
    }
}
