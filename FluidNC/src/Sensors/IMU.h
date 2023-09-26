// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#pragma once

#include <stdint.h>
#include <sstream>
#include <iomanip>
#include "../Configuration/Configurable.h"
#include "../Channel.h"
#ifdef USE_BNO085
#else
#include "util/I2Cdev.h"
#include "util/MPU6050_6Axis_MotionApps_V6_12.h"
#endif

// Definitions
//#define USE_BNO085

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

#ifdef USE_BNO085
#else
    MPU6050 *_imu_sensor;
#endif

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
