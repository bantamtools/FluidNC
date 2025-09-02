// Copyright (c) 2025 - Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "ProtectedPinTracker.h"
#include "../Logging.h"

namespace Pins {
    // Static member definition - single map instead of two
    std::unordered_map<int, ProtectedPinTracker::ProtectedPin> ProtectedPinTracker::protectedPins;

    void ProtectedPinTracker::addProtectedPin(int pinNumber, PinFunction function, const std::string& description) {
        protectedPins[pinNumber] = {function, description};
        log_debug("GPIO" << pinNumber << " reserved for " << description);
    }

    bool ProtectedPinTracker::isProtected(int pinNumber) {
        return protectedPins.find(pinNumber) != protectedPins.end();
    }

    ProtectedPinTracker::PinFunction ProtectedPinTracker::getProtectedFunction(int pinNumber) {
        auto it = protectedPins.find(pinNumber);
        return (it != protectedPins.end()) ? it->second.function : PinFunction::OTHER;
    }

    std::string ProtectedPinTracker::getProtectionReason(int pinNumber) {
        auto it = protectedPins.find(pinNumber);
        return (it != protectedPins.end()) ? it->second.description : "unknown";
    }

    void ProtectedPinTracker::clear() {
        protectedPins.clear();
        log_debug("Protected pin registrations cleared");
    }

    // More efficient: use a static array indexed by enum value
    const char* ProtectedPinTracker::functionToString(PinFunction func) {
        static const char* const names[] = {
            "I2C_SDA",
            "I2C_SCL", 
            "ENCODER_A",
            "ENCODER_B",
            "ENTER_BUTTON",
            "SD_CLK",
            "SD_CMD",
            "SD_D0",
            "SD_D1",
            "SD_D2",
            "SD_D3",
            "SD_CD",
            "IO_EXPANDER_INT",
            "OTHER"
        };
        
        size_t index = static_cast<size_t>(func);
        if (index < sizeof(names)/sizeof(names[0])) {
            return names[index];
        }
        return "UNKNOWN";
    }
}