// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license (see LICENSE).
#pragma once
#include <string>

// Neutral owner for "an SD file to run once the homing cycle triggered by a
// file-run completes." Set ONLY by the two file-run handlers (before
// run_cycles); consumed (take) in Homing::done; cleared on homing
// failure / soft-reset so a later bare $H never auto-runs a stale path.
// Single-threaded use: set/take run on the protocol path; not ISR-safe.
namespace PendingFileRun {
void        set(const std::string& path);
bool        has();
std::string take();   // returns the path and clears it
void        clear();
}
