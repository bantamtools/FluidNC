#pragma once

#include "Logging.h"

// Firmware Flashing from SD
namespace Flashing {
    void update_firmware_from_sdcard(std::string& filename);
    void update_config_from_sdcard(std::string& filename, bool addMount);
}

