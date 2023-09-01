// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#pragma once

#include <stdint.h>
#include <sstream>
#include <iomanip>
#include "../Configuration/Configurable.h"
#include "../Channel.h"
#include "ICM_20948.h"

typedef struct IMUDataType
{
    float x;
    float y;
    float z;
    float roll;
    float pitch;
} IMUDataType;

// Class
class IMU : public Configuration::Configurable {

    uint8_t _i2c_address = 0x53;
    uint8_t _i2c_num = 1;

    Pin _int_pin;

private:

    IMUDataType *_imu_data;

public:

    ICM_20948_I2C* _icm_20948;

	IMU();
    ~IMU();

    void init();
    void read();
    struct IMUDataType* get_data();
   
    // Configuration handlers
    void validate() override;
    void group(Configuration::HandlerBase& handler) override;
};
