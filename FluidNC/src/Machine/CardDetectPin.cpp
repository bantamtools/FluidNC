#include "src/Machine/CardDetectPin.h"
#include "src/Machine/EventPin.h"
#include "src/Machine/MachineConfig.h"  // config
#include "src/Protocol.h"  // protocol_send_event_from_ISR()

namespace Machine {
    CardDetectPin::CardDetectPin(Pin& pin) :
        EventPin(&cardDetectEvent, "Card Detect", &pin) {
    }

    void CardDetectPin::init() {
        EventPin::init();
        if (_pin->undefined()) {
            return;
        }
        update(get());
    }

    void CardDetectPin::update(bool value) {

        // Mount/unmount SD card based on card detect value (active-low)
        if (value) {
            log_info("1!");
            sd_unmount();
        } else {
            log_info("2!");
            sd_mount();
        }
        
        // Update the files menu based on SD listing
        sd_populate_files_menu();
    }
}
