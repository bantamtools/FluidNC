// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#pragma once

#include <stdint.h>
#include <sstream>
#include <iomanip>
#include "../Configuration/Configurable.h"
#include "../Channel.h"
#include "ICM_20948.h"

// Type definitions
typedef struct imuDataType
{
    bool valid;
    double q[4];
    int16_t accuracy;

} imuDataType;

// Class
class IMU : public Configuration::Configurable {

    uint8_t _i2c_address = 0x53;
    uint8_t _i2c_num = 1;

    Pin _int_pin;

private:

    static constexpr int    IMU_ACCURACY_MIN    = 200;
    static constexpr int    IMU_ACCURACY_MAX    = 600;
    static constexpr int    IMU_MAX_RETRIES     = 10;
    static constexpr int    IMU_READ_DELAY_MS   = 10;

    ICM_20948_I2C *_icm_20948;

public:

    imuDataType _imu_data;

	IMU();
    ~IMU();

    void init();
    void read();
   
    // Configuration handlers
    void validate() override;
    void group(Configuration::HandlerBase& handler) override;
};
