// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <limits>
#include "../Configuration/Configurable.h"
// #include "Axes.h"
#include "Motor.h"
#include "Homing.h"

namespace MotorDrivers {
    class MotorDriver;
}

namespace Machine {
    // Result of Axis::computeSoftLimitDefaults() — the soft-limit state that
    // boot derivation (and post-job restore) would produce given the current
    // config-derived inputs. Used by callers that need to compare against
    // live state before deciding whether to write through. See .
    struct SoftLimitState {
        float min     = std::numeric_limits<float>::quiet_NaN();
        float max     = std::numeric_limits<float>::quiet_NaN();
        bool  enabled = false;
    };

    class Axis : public Configuration::Configurable {
        int _axis;
        int motorsWithSwitches();

    public:
        Axis(int currentAxis) : _axis(currentAxis) {
            for (int i = 0; i < MAX_MOTORS_PER_AXIS; ++i) {
                _motors[i] = nullptr;
            }
        }

        static const int MAX_MOTORS_PER_AXIS = 2;

        Motor*  _motors[MAX_MOTORS_PER_AXIS];
        Homing* _homing = nullptr;

        float _stepsPerMm   = 80.0f;
        float _maxRate      = 1000.0f;
        float _acceleration = 25.0f;
        float _rapid_acceleration = _acceleration;
        float _maxTravel    = 1000.0f;
        bool  _softLimits   = false;
        // Soft-limit runtime state . _softMin / _softMax hold the active
        // bounds in machine coords; NaN means "cleared." _softLimitsConfig is set
        // once at boot from the config's soft_limits: true/false choice and MUST
        // NOT be modified thereafter — Axes::restoreSoftLimitDefaults() re-derives
        // against it at every M2/M30 and mc_reset, so a write here would silently
        // change the post-restore state of every subsequent job. The existing
        // _softLimits field above becomes a live cached boolean:
        // !isnan(_softMin) && !isnan(_softMax). The hot path reads _softLimits
        // directly.
        float _softMin           = std::numeric_limits<float>::quiet_NaN();
        float _softMax           = std::numeric_limits<float>::quiet_NaN();
        bool  _softLimitsConfig  = false;
        float _unwindG0     = -1.0f;  // G0 unwinding period (e.g., 360 for degrees). <=0 = disabled

        // Configuration system helpers:
        void group(Configuration::HandlerBase& handler) override;
        void afterParse() override;
        // Pure: compute what boot derivation would produce for this axis,
        // without mutating any fields. Used by Axes::restoreSoftLimitDefaults
        // to detect whether a write-through is needed.
        SoftLimitState computeSoftLimitDefaults() const;

        // Apply the result of computeSoftLimitDefaults() to the live runtime
        // fields. Called from afterParse() at boot.
        void deriveSoftLimitDefaults();

        // Checks if a motor matches this axis:
        bool hasMotor(const MotorDrivers::MotorDriver* const driver) const;
        bool hasDualMotor();

        float commonPulloff();
        float extraPulloff();
        float unwindG0() const { return _unwindG0; }

        void init();
        void config_motors();

        ~Axis();
    };
}
