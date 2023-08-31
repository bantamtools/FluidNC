// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#pragma once

#include <stdint.h>
#include "driver/pcnt.h"
#include "../Config.h"
#include "../Configuration/Configurable.h"

// Class
class Encoder : public Configuration::Configurable {

    Pin _a_pin;
    Pin _b_pin;

protected:

	pcnt_unit_t _pcnt_unit;
    int16_t _current_value = -1;
	int16_t _previous_value = -1;
	int16_t _difference = -1;

public:

	Encoder();
    ~Encoder();

    void init();
    void read();

    // Configuration handlers.
    void validate() override;
    void group(Configuration::HandlerBase& handler) override;
};
