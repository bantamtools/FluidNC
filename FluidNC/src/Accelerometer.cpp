// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#include "Accelerometer.h"
#include "Machine/MachineConfig.h"

// Initializes the accelerometer subsystem
void Accelerometer::init() {

    // Check if accelerometer configured properly
    if (_error) {
        return;
    }

    // Set up accelerometer
    _accel = new ADXL345(_i2c_address, config->_i2c[_i2c_num]);

    // Register accelerometer as channel
    allChannels.registration(this);
    setReportInterval(250);

    // Print configuration info message
    log_info(name() << " I2C Number:" << _i2c_num << " Address:" << to_hex(_i2c_address) << 
        " INT1:" << (_int1_pin.defined() ? _int1_pin.name() : "None") << 
        " INT2:" << (_int2_pin.defined() ? _int2_pin.name() : "None"));

    //TEMP: Get ID
    byte device_id = _accel->readDeviceID();
    if (device_id != 0) {
        log_info("Accel device ID: " << to_hex(device_id));
    } else {
        log_warn("No accel found");
    }

    // Set flag
    _is_active = true;
}

// Returns active flag
bool Accelerometer::is_active() {
    return _is_active;
}

// Channel functions
void Accelerometer::afterParse() {

    if (!config->_i2c[_i2c_num]) {
        log_error("i2c" << _i2c_num << " section must be defined for accelerometer");
        _error = true;
        return;
    }
}

// Configurable functions
void Accelerometer::validate() {}

void Accelerometer::group(Configuration::HandlerBase& handler) {

    handler.item("i2c_num", _i2c_num);
    handler.item("i2c_address", _i2c_address);

    handler.item("int1_pin", _int1_pin);
    handler.item("int2_pin", _int2_pin);
}
