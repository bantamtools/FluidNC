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
            sd_unmount();
        } else {
            sd_mount();
        }
        
        // Update the files menu based on SD listing
        sd_populate_files_menu();
    }
}
