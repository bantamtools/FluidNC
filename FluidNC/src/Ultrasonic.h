// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/periph_ctrl.h"
#include "driver/gpio.h"
//#include "driver/pcnt.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "Logging.h"
#include "Config.h"
#include "Configuration/Configurable.h"
#include <limits>

// Definitions
#define ULT_MGR_PRIORITY            (configMAX_PRIORITIES - 1)
#define ULT_MGR_STACK_SIZE          4096
#define ULT_MGR_PERIODIC_MS         10

// Class
class Ultrasonic : public Configuration::Configurable {

    Pin _trig_pin;
    Pin _echo_pin;

    uint32_t _pause_time_ms = 0;        // Delay after pausing
    uint32_t _pause_distance_cm = 0;    // Distance to trigger a pause

public:
	Ultrasonic();
    ~Ultrasonic();

    void init();

    // Configuration handlers.
    void validate() override;
    void group(Configuration::HandlerBase& handler) override;
    
protected:
	//pcnt_unit_t _pcnt_unit;
	//int _previous_value = 0;
    //bool _is_active = false;
};
