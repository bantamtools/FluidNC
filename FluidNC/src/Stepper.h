// Copyright (c) 2011-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
  Stepper.h - stepper motor driver: executes motion plans of planner.c using the stepper motors
*/

#include "EnumItem.h"
#include "SpindleDatatypes.h"
#include "GCode.h"
#include "Planner.h"

#include <cstdint>

// Pause state storage for parking resume functionality
struct pause_state_t {
    float target_position[MAX_N_AXIS];  // Where the block was heading
    float feed_rate;                    // Feed rate for the motion  
    SpindleSpeed spindle_speed;         // Actual spindle speed value
    SpindleState spindle;               // Spindle state
    CoolantState coolant;               // Coolant state
    int32_t line_number;                // Line number for reporting
    PlMotion motion;                    // Motion type flags (includes rapidMotion)
    bool valid;                         // Indicates if pause data is valid
};

namespace Stepper {
    void init();

    bool pulse_func();

    // Enable steppers, but cycle does not start unless called by motion control or realtime command.
    void wake_up();

    // Stops stepping and disables stepper (not ISR-safe)
    void go_idle();

    // Stops stepping (ISR-safe)
    void stop_stepping();

    // Reset the stepper subsystem variables
    void reset();

    // Changes the run state of the step segment buffer to execute the special parking motion.
    void parking_setup_buffer();

    // Restores the step segment buffer to the normal run state after a parking motion.
    void parking_restore_buffer();

    // Reloads step segment buffer. Called continuously by realtime execution system.
    void prep_buffer();

    // Called by planner_recalculate() when the executing block is updated by the new plan.
    bool update_plan_block_parameters();

    // Called by realtime status reporting if realtime rate reporting is enabled in config.h.
    float get_realtime_rate();

    extern uint32_t isr_count;
}
