// Copyright (c) 2025 - Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <unordered_set>
#include <unordered_map>
#include <string>

namespace Pins {
    /**
     * Tracks pins that are protected for critical system functionality.
     * Protected pins cannot be overridden by user configuration and are
     * essential for UI access (I2C, encoder, enter button, SD card).
     */
    class ProtectedPinTracker {
    public:
        // Pin function categories - only track critical UI functions specifically
        enum class PinFunction {
            // Critical UI functions that must be protected (order must match array below)
            I2C_SDA = 0,
            I2C_SCL,
            ENCODER_A,
            ENCODER_B,
            ENTER_BUTTON,
            SD_CLK,
            SD_CMD,
            SD_D0,
            SD_D1,
            SD_D2,
            SD_D3,
            SD_CD,
            IO_EXPANDER_INT,
            
            // Everything else (motors, limits, coolant, user outputs, etc.)
            OTHER
        };
        
    private:
        struct ProtectedPin {
            PinFunction function;
            std::string description;
        };
        
        // Change from current implementation which has separate maps
        static std::unordered_map<int, ProtectedPin> protectedPins;
        
    public:
        /**
         * Register a pin as protected for a specific function.
         * @param pinNumber GPIO pin number to protect
         * @param function The function this pin is used for
         * @param description Human-readable description
         */
        static void addProtectedPin(int pinNumber, PinFunction function, const std::string& description);
        
        /**
         * Check if a pin is protected from user configuration.
         * @param pinNumber GPIO pin number to check
         * @return true if pin is protected, false otherwise
         */
        static bool isProtected(int pinNumber);
        
        /**
         * Get the function a pin is protected for.
         * @param pinNumber GPIO pin number to query
         * @return The function, or OTHER if not protected
         */
        static PinFunction getProtectedFunction(int pinNumber);
        
        /**
         * Get human-readable description of why a pin is protected.
         * @param pinNumber GPIO pin number to query
         * @return Description string
         */
        static std::string getProtectionReason(int pinNumber);
        
        /**
         * Clear all protected pin registrations.
         * Used during config loading to reset protection state.
         */
        static void clear();
        
        /**
         * Convert PinFunction enum to string for logging.
         */
        static const char* functionToString(PinFunction func);
    };
}