// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#include "Accelerometer.h"
#include "Machine/MachineConfig.h"

// Accelerometer constructor
Accelerometer::Accelerometer() {

    // Allocate memory for accelerometer data
    _accel_data = new struct AccelDataType;
}

// Accelerometer destructor
Accelerometer::~Accelerometer() {

    // Deallocate memory for the accelerometer data
    delete(_accel_data);
}

// Initializes the accelerometer subsystem
void Accelerometer::init() {

    // Initialize the data structure
    _accel_data->x      = 0;
    _accel_data->y      = 0;
    _accel_data->z      = 0;
    _accel_data->roll   = 0;
    _accel_data->pitch  = 0;

    // Set up accelerometer
    _adxl345 = new ADXL345(_i2c_address, config->_i2c[_i2c_num]);

    // Set the data rate and data range
    if (!_adxl345->writeRate(ADXL345_RATE_200HZ)) {
        log_warn("Failed to set accelerometer rate");
    }
    if (!_adxl345->writeRange(ADXL345_RANGE_2G)) {
        log_warn("Failed to set accelerometer range");
    }

    // Start the accelerometer
    if(!_adxl345->start()) {
        log_warn("Accelerometer failed to start");
    }

    // Print configuration info message
    log_info("Accelerometer: I2C Number:" << _i2c_num << " Address:" << to_hex(_i2c_address) << 
        " INT1:" << (_int1_pin.defined() ? _int1_pin.name() : "None") << 
        " INT2:" << (_int2_pin.defined() ? _int2_pin.name() : "None"));
}

// Reads the latest values from the accelerometer
void Accelerometer::read() {

    // Read new values when an update is available
    if (_adxl345->update()) {

        _accel_data->x = _adxl345->getXMeterPerSec2();
        _accel_data->y = _adxl345->getYMeterPerSec2();
        _accel_data->z = _adxl345->getZMeterPerSec2();

        float _x = _adxl345->getXGs();
        float _y = _adxl345->getYGs();
        float _z = _adxl345->getZGs();

        _accel_data->roll = atan2(_y, sqrt((_x * _x) + (_z * _z))) * (180.0 / PI);
        _accel_data->pitch = atan2(_x, sqrt((_y * _y) + (_z * _z))) * (180.0 / PI);
        
        log_info("X: " << _accel_data->x << ", Y: " << _accel_data->y << ", Z: " << _accel_data->z <<
                 ", Roll = " << _accel_data->roll << ", Pitch = " << _accel_data->pitch);

        delay_ms(500);
    }
}

// Returns the current accelerometer data
struct AccelDataType* Accelerometer::get_data() {
    return _accel_data;
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
