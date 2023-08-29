// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#include "Accelerometer.h"
#include "Machine/MachineConfig.h"

// Accelerometer constructor
Accelerometer::Accelerometer() {}

// Accelerometer destructor
Accelerometer::~Accelerometer() {}

// Accelerometer read task
/*
void Accelerometer::read_task(void *pvParameters) {

    // Connect pointer
    Accelerometer* instance = static_cast<Accelerometer*>(pvParameters);

    // Loop forever
    while(1) {

        // Read new values when an update is available
        if (instance->_accel->update()) {

            //log_info("X: " << instance->_accel->getRawX() << ", Y: " << instance->_accel->getRawY() << ", Z: " << instance->_accel->getRawZ());
        }

        // Check every 100ms
        vTaskDelay(ACCEL_READ_PERIODIC_MS/portTICK_PERIOD_MS);
    }
}*/

// Initializes the accelerometer subsystem
void Accelerometer::init() {

    // Set up accelerometer
    _accel = new ADXL345(_i2c_address, config->_i2c[_i2c_num]);

    // Set the data rate and data range
    if (!_accel->writeRate(ADXL345_RATE_200HZ)) {
        log_warn("Failed to set accelerometer rate");
    }
    if (!_accel->writeRange(ADXL345_RANGE_2G)) {
        log_warn("Failed to set accelerometer range");
    }

    // Start the accelerometer
    if(!_accel->start()) {
        log_warn("Accelerometer failed to start");
    }

    // Print configuration info message
    log_info("Accelerometer: I2C Number:" << _i2c_num << " Address:" << to_hex(_i2c_address) << 
        " INT1:" << (_int1_pin.defined() ? _int1_pin.name() : "None") << 
        " INT2:" << (_int2_pin.defined() ? _int2_pin.name() : "None"));
}

// Configurable functions
void Accelerometer::validate() {

    if (!config->_i2c[_i2c_num]) {
        Assert((config->_i2c[_i2c_num]), "Accelerometer I2C section [i2c%d] not defined.", _i2c_num);
    }

    if (!_int1_pin.undefined() || !_int2_pin.undefined()) {
        Assert(!_int1_pin.undefined(), "Accelerometer INT1 pin should be configured.");
        Assert(!_int2_pin.undefined(), "Accelerometer INT2 pin should be configured.");
    }
}

void Accelerometer::group(Configuration::HandlerBase& handler) {

    handler.item("i2c_num", _i2c_num);
    handler.item("i2c_address", _i2c_address);

    handler.item("int1_pin", _int1_pin);
    handler.item("int2_pin", _int2_pin);
}
