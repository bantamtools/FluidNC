// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Encoder.h"
#include "Ultrasonic.h"
#include "Accelerometer.h"
#include "Config.h"
#include "Configuration/Configurable.h"

// Class
class Sensors : public Configuration::Configurable {

private:

    static constexpr UBaseType_t    SNS_READ_PRIORITY       = (configMAX_PRIORITIES - 3);
    static constexpr uint32_t       SNS_READ_STACK_SIZE     = 4096;
    static constexpr uint32_t       SNS_READ_PERIODIC_MS    = 10;

    static void read_task(void *pvParameters);

protected:

public:

    Encoder         *_encoder = nullptr;
    Ultrasonic      *_ultrasonic = nullptr;
    Accelerometer   *_accelerometer = nullptr;

	Sensors();
    ~Sensors();

    void init();

    // Configuration handlers.
    void validate() override;
    void group(Configuration::HandlerBase& handler) override;
    void afterParse() override;
};
