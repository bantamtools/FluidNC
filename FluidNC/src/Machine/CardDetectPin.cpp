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
        
        // Print message to channel to trigger updates
        log_info("SD Card Detect Event");
    }
}
