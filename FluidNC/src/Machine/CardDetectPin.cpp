#include "src/Machine/CardDetectPin.h"
#include "src/Machine/EventPin.h"
#include "src/Machine/MachineConfig.h"  // config
#include "src/Protocol.h"  // protocol_send_event_from_ISR()
#include "src/WifiSetupFile.h"
#ifdef USE_SDMMC
#include "Driver/sdmmc.h"
#else
#include "Driver/sdspi.h"
#endif

namespace Machine {
    CardDetectPin::CardDetectPin(Pin& pin) :
        EventPin(&cardDetectEvent, "Card Detect", &pin) {
    }

    void CardDetectPin::init() {
        EventPin::init();
        if (_pin->undefined()) {
            return;
        }
        // Record physical presence from the pin before update() is called.
        // update() skips side-effects (mount/menu) until systemIsInitialized,
        // but the fail-fast mount guard in sd_mount() relies on sd_cd_pin_present
        // being set from real hardware at boot. Pin active-low: high == absent,
        // low == present, matching update()'s own convention.
        sd_set_card_present(!get());
        update(get());
    }

    void CardDetectPin::update(bool value) {
        if(!config->_systemIsInitialized){
            return;
        }

        log_debug("Update in card detect pin");
        // Mount/unmount SD card based on card detect value (active-low).
        // Notify the driver of physical presence before attempting any
        // mount so sd_mount() can skip the blocking VFS call when absent.
        if (value) {
            sd_set_card_present(false);
            sd_unmount();
        } else {
            sd_set_card_present(true);
            sd_mount();
            // Check for wifi_setup.txt after mount (may reboot)
            check_wifi_setup_file();
        }

        // Update the files menu based on SD listing
        sd_populate_files_menu();
    }
}
