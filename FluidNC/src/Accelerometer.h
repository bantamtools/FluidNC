// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#pragma once

#include <stdint.h>
//#include "Config.h"
#include "Configuration/Configurable.h"
#include "Channel.h"

// Definitions
//TBD

// Class
class Accelerometer : public Channel, public Configuration::Configurable {

    Pin _scl_pin;
    Pin _sda_pin;
    uint32_t _i2c_addr = 0x0;

    Pin _int1_pin;  
    Pin _int2_pin;

public:
    Accelerometer() : Channel("accelerometer") {}

    Accelerometer(const Accelerometer&) = delete;
    Accelerometer(Accelerometer&&)      = delete;
    Accelerometer& operator=(const Accelerometer&) = delete;
    Accelerometer& operator=(Accelerometer&&) = delete;

    virtual ~Accelerometer() = default;

    void init();
    bool is_active();

    // Configuration handlers.
    void validate() override;
    void group(Configuration::HandlerBase& handler) override;
    
protected:

    bool _is_active = false;

};
