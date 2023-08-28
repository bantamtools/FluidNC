// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#include "Accelerometer.h"
#include "Machine/MachineConfig.h"

// Accelerometer constructor
Accelerometer::Accelerometer() {

    _is_active = false;
}

// Accelerometer destructor
Accelerometer::~Accelerometer() {}

// Accelerometer read task
void Accelerometer::read_task(void *pvParameters) {

    // Connect pointer
    Accelerometer* instance = static_cast<Accelerometer*>(pvParameters);

    // Loop forever
    while(1) {

        // Read new values when an update is available
        if (instance->_accel->update()) {

            log_info("X: " << instance->_accel->getRawX() << ", Y: " << instance->_accel->getRawY() << ", Z: " << instance->_accel->getRawZ());
        }

        // Check every 100ms
        vTaskDelay(ACCEL_READ_PERIODIC_MS/portTICK_PERIOD_MS);
    }
}

// Initializes the accelerometer subsystem
void Accelerometer::init() {

    // Check if accelerometer configured properly
    if (_error) {
        return;
    }

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

    //TEMP: Get ID
    byte device_id = _accel->readDeviceID();
    if (device_id != 0) {
        log_info("Accel device ID: " << to_hex(device_id));
    } else {
        log_warn("No accel found");
    }

    // Start read task
    xTaskCreate(read_task, "accel_read_task", ACCEL_READ_STACK_SIZE, this, ACCEL_READ_PRIORITY, NULL);

    // Set flag
    _is_active = true;
}

// Returns active flag
bool Accelerometer::is_active() {
    return _is_active;
}

// Configurable functions
void Accelerometer::validate() {}

void Accelerometer::afterParse() {

    if (!config->_i2c[_i2c_num]) {
        log_error("i2c" << _i2c_num << " section must be defined for accelerometer");
        _error = true;
        return;
    }
}

void Accelerometer::group(Configuration::HandlerBase& handler) {

    handler.item("i2c_num", _i2c_num);
    handler.item("i2c_address", _i2c_address);

    handler.item("int1_pin", _int1_pin);
    handler.item("int2_pin", _int2_pin);
}
