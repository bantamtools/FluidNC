// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#include "Sensors.h"
#include "../Machine/MachineConfig.h"

// Sensors constructor
Sensors::Sensors() {
}

// Sensors destructor
Sensors::~Sensors() {}

// Sensors read task
void Sensors::read_task(void *pvParameters) {

    // Connect pointer
    Sensors* instance = static_cast<Sensors*>(pvParameters);

    // Loop forever
    while(1) {

        // Read sensor inputs
        if (instance->_encoder) {
            instance->_encoder->read();
        }
        if (instance->_ultrasonic) {
            instance->_ultrasonic->read();
        }
        if (instance->_imu) {
            instance->_imu->read();
        }

        // Check every 10ms
        vTaskDelay(SNS_READ_PERIODIC_MS/portTICK_PERIOD_MS);
    }
}

// Initializes the sensors subsystem
void Sensors::init() {

    // Initialize configured sensors
    if (_encoder) {
        _encoder->init();
    }
    if (_ultrasonic) {
        _ultrasonic->init();
    }
    if (_imu) {
        _imu->init();
    }

    // Start read task
    xTaskCreate(read_task, "sensors_read_task", SNS_READ_STACK_SIZE, this, SNS_READ_PRIORITY, NULL);
}

// Configurable functions
void Sensors::validate() {}

void Sensors::group(Configuration::HandlerBase& handler) {

    handler.section("encoder", _encoder);
    handler.section("ultrasonic", _ultrasonic);
    handler.section("imu", _imu);
}
