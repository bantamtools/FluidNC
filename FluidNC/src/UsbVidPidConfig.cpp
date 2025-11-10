// Copyright (c) 2025 - Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Config.h"

#ifdef ESP_PLATFORM
#ifdef USE_USB_RUNTIME_CONFIG

#include <nvs.h>
#include <nvs_flash.h>
#include <USB.h>

// Class that configures USB VID/PID from NVS before Arduino calls USB.begin()
// Uses init_priority to ensure constructor runs before app_main()
class UsbVidPidConfigurator {
public:
    UsbVidPidConfigurator() {
        // Initialize NVS if needed
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            err = nvs_flash_init();
        }

        if (err != ESP_OK) {
            return;  // NVS init failed - use default VID/PID
        }

        // Read USB VID/PID from NVS
        nvs_handle_t handle;
        err = nvs_open("FluidNC", NVS_READONLY, &handle);
        if (err != ESP_OK) {
            return;  // No stored config - use default VID/PID
        }

        uint16_t vid = 0;
        uint16_t pid = 0;
        bool hasVid = (nvs_get_u16(handle, "usb_vid", &vid) == ESP_OK);
        bool hasPid = (nvs_get_u16(handle, "usb_pid", &pid) == ESP_OK);

        nvs_close(handle);

        // Configure USB VID/PID before Arduino calls USB.begin()
        if (hasVid && hasPid) {
            USB.VID(vid);
            USB.PID(pid);
        }
    }
};

// Global instance with init_priority(500) to run before Arduino's USB.begin()
__attribute__((init_priority(500))) UsbVidPidConfigurator usbVidPidConfigurator;

#endif // USE_USB_RUNTIME_CONFIG
#endif // ESP_PLATFORM
