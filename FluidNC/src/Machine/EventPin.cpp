#include "EventPin.h"

#include "src/Report.h"    // addPinReport
#include "src/Protocol.h"  // event_queue
#include "src/System.h"    // sys

#include "Driver/fluidnc_gpio.h"

namespace Machine {
    EventPin::EventPin(Event* event, const char* legend, Pin* pin) : _event(event), _legend(legend), _pin(pin), _locked(false) {}
    bool EventPin::get() { return _pin->read(); }

    void EventPin::gpioAction(int gpio_num, void* arg, bool active) {
        EventPin* obj = static_cast<EventPin*>(arg);
        obj->update(active);
        if (active && !obj->_locked) {
            protocol_send_event(obj->_event, obj);
        }
    }

    void EventPin::init() {
        if (_pin->undefined()) {

            // Encoder button not configured, use MVP as fail-safe default
            if (_legend.compare("enter_pin") == 0) {
                *_pin = Pin::create("gpio.36");
                _fail_safe = true;
            } else {
                return;
            }
        }

        _pin->report(_legend);

        auto attr = Pin::Attr::Input;
        _pin->setAttr(attr);
        _gpio = _pin->getNative(Pin::Capabilities::Input);
        gpio_set_action(_gpio, gpioAction, (void*)this, _pin->getAttr().has(Pin::Attr::ActiveLow));
        
        // Lock out event pins in fail-safe mode
        _locked = _fail_safe;
    }

    bool EventPin::locked() {
        return _locked;
    }

    void EventPin::lock() {
        _locked = true;
    }

    void EventPin::unlock() {
        _locked = false;
    }

    EventPin::~EventPin() { gpio_clear_action(_gpio); }
};
