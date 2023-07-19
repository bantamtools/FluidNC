#pragma once

#include "EventPin.h"

namespace Machine {
    class CardDetectPin : public EventPin {
    private:
        bool     _value   = 0;

    public:
        CardDetectPin(Pin& pin);

        void update(bool value) override;

        void init();
    };
}
