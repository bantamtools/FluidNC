// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#pragma once

#include <stdint.h>
#include <sstream>
#include <iomanip>
#include "Configuration/Configurable.h"
#include "Channel.h"
#include "ADXL345.h"

// Class
class Accelerometer : public Configuration::Configurable {

    uint8_t _i2c_address = 0x53;
    uint8_t _i2c_num = 1;

    Pin _int1_pin;  
    Pin _int2_pin;

public:

    ADXL345* _accel;

	Accelerometer();
    ~Accelerometer();

    void init();
    void read() {}; // TEMP
   
    // Configuration handlers
    void validate() override;
    void group(Configuration::HandlerBase& handler) override;
};
