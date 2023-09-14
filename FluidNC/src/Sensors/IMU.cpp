// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#include "IMU.h"
#include "../Machine/MachineConfig.h"

// IMU constructor
IMU::IMU() {

    // Allocate memory for IMU
    _mpu_6050 = new MPU6050(config->_i2c[_i2c_num], _i2c_address);

    // Mark DMP uninitialized
    _dmp_ready = false;
}

// IMU destructor
IMU::~IMU() {

    // Deallocate memory for the IMU
    delete(_mpu_6050);

    // Mark DMP uninitialized
    _dmp_ready = false;
}

// Initializes the IMU subsystem
void IMU::init() {

    bool success = false; // Use success to show if IMU connection was successful
    uint8_t dmp_status = 1;

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
        log_info("IMU: I2C Number:" << _i2c_num << " Address:" << to_hex(_i2c_address) << 
            " INT:" << (_int_pin.defined() ? _int_pin.name() : "None"));
    }

    // Load and configure the DMP
    dmp_status = _mpu_6050->dmpInitialize();

    // Initialized, now calibrate and enable DMP...
    if (dmp_status == 0) {

        // Calibration time, generate offsets and calibrate
        _mpu_6050->CalibrateAccel(10);
        _mpu_6050->CalibrateGyro(10);
        _mpu_6050->PrintActiveOffsets();

        // Turn on the DMP now that it's ready
        _mpu_6050->setDMPEnabled(true);

        // Set ready flag and print success message
        _dmp_ready = true;
        log_info("IMU: DMP initialized!")

    // Error, print message
    } else {

        // 1 = initial memory load failed
        // 2 = DMP configuration updates failed
        log_warn("IMU: DMP initialization failed, error code = " << dmp_status)
    }
}

// Reads the latest values from the IMU
void IMU::read() {

    // Obtain the lock
    _mutex.lock();

    // DEBUG: Read and display raw accel/gyro measurements from IMU
    //int16_t ax, ay, az, gx, gy, gz;
    //_mpu_6050->getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
    //log_info("a/g: [" << ax << " " << ay << " " << az << "]  [" << gx << " " << gy << " " << gz << "]");
    //delay_ms(100);

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
