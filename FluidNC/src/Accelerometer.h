// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#pragma once

#include <stdint.h>
#include "Configuration/Configurable.h"
#include "Channel.h"
#include "ADXL345.h"

// Definitions
//TBD

// Class
class Accelerometer : public Channel, public Configuration::Configurable {

    uint8_t _i2c_address = 0x53;
    uint8_t _i2c_num = 1;

    Pin _int1_pin;  
    Pin _int2_pin;

    bool _error = false;

public:

    ADXL345* _accel;

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
    void afterParse() override;
    void group(Configuration::HandlerBase& handler) override;
    
protected:

    bool _is_active = false;

};
