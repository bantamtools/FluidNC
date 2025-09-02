// Copyright (c) 2025 - Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <cstdint>
#include <string>

namespace Machine {
    // Board types based on hardware detection
    enum class BoardType {
        Hen,       // No I/O expander detected
        Rooster    // TCA6408 I/O expander detected at 0x20
    };
    
    // Main detection function - must be called very early in boot
    // before any I2C or config initialization
    BoardType detectBoardType();
    
    // Reset detection state (useful for testing/recovery)
    void resetBoardDetection();
    
    // Get cached result without re-probing hardware
    BoardType getDetectedBoardType();
    
    // Get human-readable board name for logging
    const char* getBoardTypeName(BoardType type);

} // namespace Machine

// Generated pin constants (from build-time extraction)
// Include generated pin definitions which define their own Machine::CriticalPins namespace
#include "BoardDetection_pins.inc"