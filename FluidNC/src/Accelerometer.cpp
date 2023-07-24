// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#include "Accelerometer.h"

// Initializes the accelerometer subsystem
void Accelerometer::init() {

    // Check if accelerometer I2C pins configured
    if (!_scl_pin.defined() || !_sda_pin.defined() || !_i2c_addr) {
        _is_active = false;
        return;
    }

    // Set up accelerometer pins, address, i2c, etc...
    //TBD

    // Register accelerometer as channel
    allChannels.registration(this);
    setReportInterval(250);

    // Print configuration info message
    log_info(name() << "I2C Address: " << to_hex(_i2c_addr) << 
        " SCL:" << _scl_pin.name() << " SDA:" << _sda_pin.name() << 
        " INT1:" << (_int1_pin.defined() ? _int1_pin.name() : "None") << 
        " INT2:" << (_int2_pin.defined() ? _int2_pin.name() : "None"));

    // Set flag
    _is_active = true;
}

// Returns active flag
bool Accelerometer::is_active() {
    return _is_active;
}

// Configurable functions
void Accelerometer::validate() {}

void Accelerometer::group(Configuration::HandlerBase& handler) {

    handler.item("scl_pin", _scl_pin);
    handler.item("sda_pin", _sda_pin);
    handler.item("i2c_addr", _i2c_addr);

    handler.item("int1_pin", _int1_pin);
    handler.item("int2_pin", _int2_pin);
}
