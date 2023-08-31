// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#include "IMU.h"
#include "../Machine/MachineConfig.h"

TwoWire _i2c_port = TwoWire(1);

// IMU constructor
IMU::IMU() {

    _icm20948 = new ICM_20948_I2C();

    // Allocate memory for IMU data
    _imu_data = new struct IMUDataType;
}

// IMU destructor
IMU::~IMU() {

    delete (_icm20948);

    // Deallocate memory for the I2C and IMU data
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

    auto sdaPin = 43;//config->_i2c[_i2c_num]->_sda.getNative(Pin::Capabilities::Native | Pin::Capabilities::Input | Pin::Capabilities::Output);
    auto sclPin = 44;//config->_i2c[_i2c_num]->_scl.getNative(Pin::Capabilities::Native | Pin::Capabilities::Input | Pin::Capabilities::Output);
    
    _icm20948->enableDebugging();
/*
    // Set up IMU
    log_info("ICM BEGIN > " << _icm20948->begin(true));

    _icm20948->statusString();
    if (_icm20948->status == ICM_20948_Stat_Ok) {
        log_info("IMU OK!");
    } else {
        log_info("IMU ERROR!");
    }
  */ 
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

   // if (!config->_i2c[_i2c_num]) {
   //     Assert((config->_i2c[_i2c_num]), "IMU I2C section [i2c%d] not defined.", _i2c_num);
   // }

    if (!_int_pin.undefined()) {
        Assert(!_int_pin.undefined(), "IMU INT pin should be configured.");
    }
}

void IMU::group(Configuration::HandlerBase& handler) {

    handler.item("i2c_num", _i2c_num);
    handler.item("i2c_address", _i2c_address);

    handler.item("int_pin", _int_pin);
}
