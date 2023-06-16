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
#include <limits>

// Definitions
#define ENC_A_PIN                   (gpio_num_t)35
#define ENC_B_PIN                   (gpio_num_t)48

#define ENC_MGR_PRIORITY            (configMAX_PRIORITIES - 1)
#define ENC_MGR_STACK_SIZE          4096
#define ENC_MGR_PERIODIC_MS         10

// Class
class Encoder {

public:
	Encoder(gpio_num_t a_pin  = ENC_A_PIN, gpio_num_t b_pin = ENC_B_PIN, pcnt_unit_t pcnt_unit = PCNT_UNIT_0);
	void init();
	int16_t get_value();
    int16_t get_difference();

protected:
    gpio_num_t a_pin, b_pin;
	pcnt_unit_t pcnt_unit;
	int previous_value;
};
