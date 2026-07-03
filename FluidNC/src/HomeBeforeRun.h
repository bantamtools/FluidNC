// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license (see LICENSE).
#pragma once

// Pure decision shared by every file-run launcher: should the run auto-home
// before opening the file, given current machine state? Extracted from the
// launchers so the truth table is unit-testable without the hardware-coupled
// config/Kinematics/Homing dependencies (see tests/HomeBeforeRunTest.cpp).
//
//   homed               - config->_axes->_homed
//   canHome             - config->_kinematics->canHome(0)
//   hasRealHomingCycles - config->_axes->hasRealHomingCycles()
//   eggbot              - machine type is EggBot (never homes)
bool protocol_should_home_before_run(bool homed, bool canHome, bool hasRealHomingCycles, bool eggbot);
