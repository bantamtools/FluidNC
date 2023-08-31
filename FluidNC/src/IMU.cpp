// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#include "IMU.h"
#include "Machine/MachineConfig.h"

// IMU constructor
IMU::IMU() {

    // Allocate memory for IMU data
    _imu_data = new struct IMUDataType;
}

// IMU destructor
IMU::~IMU() {

    // Deallocate memory for the IMU data
    delete(_imu_data);
}

// Initializes the IMU subsystem
void IMU::init() {

    // Initialize the data structure
    _imu_data->x      = 0;
    _imu_data->y      = 0;
    _imu_data->z      = 0;
    _imu_data->roll   = 0;
    _imu_data->pitch  = 0;

    // Set up IMU

    // Setup

    // Start the IMU
    
    // Print configuration info message
    log_info("IMU: I2C Number:" << _i2c_num << " Address:" << to_hex(_i2c_address) << 
        " INT:" << (_int_pin.defined() ? _int_pin.name() : "None"));
}

// Reads the latest values from the IMU
void IMU::read() {

    // Read new values when an update is available
}

// Returns the current IMU data
struct IMUDataType* IMU::get_data() {
    return _imu_data;
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
