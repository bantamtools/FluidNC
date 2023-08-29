// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Encoder.h"
#include "Ultrasonic.h"
#include "Accelerometer.h"
#include "Config.h"

//#include "freertos/queue.h"
//#include "driver/periph_ctrl.h"
//#include "driver/gpio.h"
//#include "driver/pcnt.h"
//#include "esp_attr.h"
//#include "esp_log.h"
//#include "Logging.h"
//#include "Configuration/Configurable.h"
//#include <limits>

// Class
class Sensors {

private:

    static constexpr UBaseType_t    SNS_READ_PRIORITY       = (configMAX_PRIORITIES - 3);
    static constexpr uint32_t       SNS_READ_STACK_SIZE     = 4096;
    static constexpr uint32_t       SNS_READ_PERIODIC_MS    = 10;

    Encoder         *_encoder;
    Ultrasonic      *_ultrasonic;
    Accelerometer   *_accelerometer;

    static void read_task(void *pvParameters);

protected:

public:

	Sensors();
    ~Sensors();

    void init();
};
