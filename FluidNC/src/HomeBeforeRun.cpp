// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license (see LICENSE).
#include "HomeBeforeRun.h"

bool protocol_should_home_before_run(bool homed, bool canHome, bool hasRealHomingCycles, bool eggbot) {
    // Home first only when: not already homed, the machine can home, the config
    // has real (non-set_mpos_only) homing cycles that reach Homing::done() and
    // open the pending file, and it isn't an EggBot (which never homes).
    return !homed && canHome && hasRealHomingCycles && !eggbot;
}
