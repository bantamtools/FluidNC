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

// Class
class Encoder : public Configuration::Configurable {

    Pin _a_pin;
    Pin _b_pin;

private:

    static constexpr UBaseType_t    ENC_READ_PRIORITY       = (configMAX_PRIORITIES - 3);
    static constexpr uint32_t       ENC_READ_STACK_SIZE     = 1536;
    static constexpr uint32_t       ENC_READ_PERIODIC_MS    = 10;

    static void read_task(void *pvParameters);

protected:

	pcnt_unit_t _pcnt_unit;
    bool _is_active = false;
    bool _ready_flag = false;  // Flag that read task asserts when value ready to read
    int16_t _current_value = -1;
	int16_t _previous_value = -1;
	int16_t _difference = -1;

public:

	Encoder();
    ~Encoder();

    void init();
    int16_t get_difference();
    bool is_active();

    // Configuration handlers.
    void validate() override;
    void group(Configuration::HandlerBase& handler) override;
};
