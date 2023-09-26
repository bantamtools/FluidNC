// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#pragma once

#include <stdint.h>
#include <sstream>
#include <iomanip>
#include "../Configuration/Configurable.h"
#include "../Channel.h"
#include "util/I2Cdev.h"
#include "util/MPU6050_6Axis_MotionApps_V6_12.h"

// Type definitions
typedef struct imuDataType
{
    float yaw;
    float pitch;
    float roll;

} imuDataType;

// Class
class IMU : public Configuration::Configurable {

    uint8_t _i2c_address = 0x68;
    uint8_t _i2c_num = 1;

    Pin _int_pin;

private:

    MPU6050 *_imu_sensor;

public:

    imuDataType _imu_data;
    std::mutex _mutex;

	IMU();
    ~IMU();

    void init();
    void read();
   
    // Configuration handlers
    void validate() override;
    void group(Configuration::HandlerBase& handler) override;
};
