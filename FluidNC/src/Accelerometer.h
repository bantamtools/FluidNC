// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#pragma once

#include <stdint.h>
#include <sstream>
#include <iomanip>
#include "Configuration/Configurable.h"
#include "Channel.h"
#include "ADXL345.h"

typedef struct AccelDataType
{
    float x;
    float y;
    float z;
    float roll;
    float pitch;
} AccelDataType;

// Class
class Accelerometer : public Configuration::Configurable {

    uint8_t _i2c_address = 0x53;
    uint8_t _i2c_num = 1;

    Pin _int1_pin;  
    Pin _int2_pin;

private:

    AccelDataType *_accel_data;

public:

    ADXL345* _adxl345;

	Accelerometer();
    ~Accelerometer();

    void init();
    void read();
    struct AccelDataType* get_data();
   
    // Configuration handlers
    void validate() override;
    void group(Configuration::HandlerBase& handler) override;
};
