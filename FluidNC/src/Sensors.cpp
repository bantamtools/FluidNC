// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#include "Sensors.h"

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

        //TODO

        // Check every 10ms
        vTaskDelay(SNS_READ_PERIODIC_MS/portTICK_PERIOD_MS);
    }
}

// Initializes the sensors subsystem
void Sensors::init() {

    // Initialize the sensors
    _encoder = new Encoder();
    _ultrasonic = new Ultrasonic();
    _accelerometer = new Accelerometer();

    // Start read task
    xTaskCreate(read_task, "sensors_read_task", SNS_READ_STACK_SIZE, this, SNS_READ_PRIORITY, NULL);
}
