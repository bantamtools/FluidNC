#pragma once

#include "Logging.h"

#include <string>

// Firmware Flashing from SD
namespace Flashing {
    // Why an update did not happen, for the OLED.
    struct Failure {
        const char* reason;      // short, at most two lines
        bool        persistent;  // device state was changed (e.g. config.yaml truncated):
                                 // keep the message up until the operator dismisses it
    };

    // `path` is the SD-relative path of the selected file ("/firmware.bin" or
    // "/folder/firmware.bin", no "/sd" prefix). On success the device restarts and
    // these never return. On failure they log the detail and return the reason.
    Failure update_firmware_from_sdcard(const std::string& path);
    Failure update_config_from_sdcard(const std::string& path);
}
