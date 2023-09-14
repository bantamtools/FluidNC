// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#include "IMU.h"
#include "../Machine/MachineConfig.h"

// IMU constructor
IMU::IMU() {

    // Allocate memory for IMU
    _mpu_6050 = new MPU6050(config->_i2c[_i2c_num], _i2c_address);
}

// IMU destructor
IMU::~IMU() {

    // Deallocate memory for the IMU
    delete(_mpu_6050);
}

// Initializes the IMU subsystem
void IMU::init() {

    bool success = false; // Use success to show if IMU connection was successful

    // Initialize the IMU data structure
    _imu_data.yaw = 0.0;
    _imu_data.pitch = 0.0;
    _imu_data.roll = 0.0;

    // Initialize the IMU
    _mpu_6050->initialize();

    // Verify connection
    success = _mpu_6050->testConnection();

    // Print configuration info message if successful
    if (!success) {
        log_warn("IMU: Connection failed");
    } else {
        log_info("IMU (DMP Enabled): I2C Number:" << _i2c_num << " Address:" << to_hex(_i2c_address) << 
            " INT:" << (_int_pin.defined() ? _int_pin.name() : "None"));
    }
}

// Reads the latest values from the IMU
void IMU::read() {

    int16_t ax, ay, az;
    int16_t gx, gy, gz;

    // Obtain the lock
    _mutex.lock();

    // DEBUG: Read and display raw accel/gyro measurements from IMU
    _mpu_6050->getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
    log_info("a/g: [" << ax << " " << ay << " " << az << "]  [" << gx << " " << gy << " " << gz << "]");
    delay_ms(100);

    // Return the lock
    _mutex.unlock();
}

// Configurable functions
void IMU::validate() {

    if (!config->_i2c[_i2c_num]) {
        Assert((config->_i2c[_i2c_num]), "IMU I2C section [i2c%d] not defined.", _i2c_num);
    }

    if (!_int_pin.undefined()) {
        Assert(!_int_pin.undefined(), "IMU INT pin should be configured.");
    }
}

void IMU::group(Configuration::HandlerBase& handler) {

    handler.item("i2c_num", _i2c_num);
    handler.item("i2c_address", _i2c_address);

    handler.item("int_pin", _int_pin);
}
