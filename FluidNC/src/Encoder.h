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
#include "System.h"
#include "Configuration/Configurable.h"
#include <limits>

// Class
class Encoder : public Configuration::Configurable {

    Pin _a_pin;
    Pin _b_pin;

private:

    static void IRAM_ATTR encoder_read_cb(void *args);

protected:

	pcnt_unit_t _pcnt_unit;
	int16_t _difference = 0;

public:

	Encoder();
    ~Encoder();

    void init();
    int16_t get_difference();

    // Configuration handlers.
    void validate() override;
    void group(Configuration::HandlerBase& handler) override;
};
