// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#pragma once

#include <stdint.h>
#include "Configuration/Configurable.h"

// Class
class Ultrasonic : public Configuration::Configurable {

    Pin _trig_pin;
    Pin _echo_pin;

    uint32_t _pause_time_ms = 0;        // Delay after pausing
    uint32_t _pause_distance_cm = 0;    // Distance to trigger a pause

public:
	Ultrasonic();
    ~Ultrasonic();

    void init();

    // Configuration handlers.
    void validate() override;
    void group(Configuration::HandlerBase& handler) override;
    
protected:

};
