// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/periph_ctrl.h"
#include "driver/gpio.h"
#include "driver/pcnt.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "Logging.h"
#include "Config.h"
#include "Configuration/Configurable.h"
#include <limits>

// Definitions
#define ENC_MGR_PRIORITY            (configMAX_PRIORITIES - 1)
#define ENC_MGR_STACK_SIZE          4096
#define ENC_MGR_PERIODIC_MS         10

// Class
class Encoder : public Configuration::Configurable {

    Pin _a_pin;
    Pin _b_pin;

public:
	Encoder();
    ~Encoder();

    void init();
	int16_t get_value();
    int16_t get_difference();
    bool is_active();

    // Configuration handlers.
    void validate() override;
    void group(Configuration::HandlerBase& handler) override;
    
protected:
	pcnt_unit_t pcnt_unit;
	int _previous_value = 0;
    bool _is_active = false;
};
