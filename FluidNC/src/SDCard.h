// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Configuration/Configurable.h"
#include "WebUI/Authentication.h"
#include "Pin.h"
#include "Machine/CardDetectPin.h"
#include "Error.h"

#include <cstdint>

class SDCard : public Configuration::Configurable {
public:
    enum class State : uint8_t {
        Idle          = 0,
        NotPresent    = 1,
        Busy          = 2,
        BusyUploading = 3,
        BusyParsing   = 4,
        BusyWriting   = 5,
        BusyReading   = 6,
    };

private:
    State _state;
    int   _width = 1;
    Pin   _clk, _cmd, _d0, _d1, _d2, _d3, _cd;

    uint32_t _frequency_hz = 8000000;  // Set to nonzero to override the default

public:
    SDCard();
    SDCard(const SDCard&) = delete;
    SDCard& operator=(const SDCard&) = delete;

    const char* filename();

    // Initializes pins.
    void init();

    // Configuration handlers.
    void group(Configuration::HandlerBase& handler) override {

        handler.item("width", _width);

        handler.item("clk_pin", _clk);
        handler.item("cmd_pin", _cmd);
        handler.item("d0_pin", _d0);
        handler.item("d1_pin", _d1);
        handler.item("d2_pin", _d2);
        handler.item("d3_pin", _d3);
        handler.item("cd_pin", _cd);

        handler.item("frequency_hz", _frequency_hz, 400000, 50000000);
    }

    void validate() override;

    ~SDCard();
};
