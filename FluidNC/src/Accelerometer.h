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


    // Channel method overrides - TODO
    size_t write(uint8_t data) { return -1; }

    int read(void) override { return -1; }
    int peek(void) override { return -1; }

    Channel* pollLine(char* line) override { return nullptr; }
    void     flushRx() override {}

    bool   lineComplete(char*, char) override { return false; }
    size_t timedReadBytes(char* buffer, size_t length, TickType_t timeout) override { return 0; }

    // Configuration handlers
    void validate() override;
    void afterParse() override {}  // TODO
    void group(Configuration::HandlerBase& handler) override;
    
protected:

    bool _is_active = false;

};
