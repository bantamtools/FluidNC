/*
 * Copyright (c) 2016 Ruslan V. Uss <unclerus@gmail.com>
 * Copyright (c) 2023 Matt Staniszewski <matt.staniszewski@bantamtools.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of itscontributors
 *    may be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file Ultrasonic.h
 * @defgroup Ultrasonic Ultrasonic
 * @{
 *
 * ESP-IDF driver for ultrasonic range meters, e.g. HC-SR04, HY-SRF05 and so on
 *
 * Ported from esp-open-rtos
 *
 * Copyright (c) 2016 Ruslan V. Uss <unclerus@gmail.com>
 *
 * BSD Licensed as described in the file LICENSE at https://github.com/UncleRus/esp-idf-lib/tree/master/components/ultrasonic
 */

#pragma once

#include <stdint.h>
#include "Configuration/Configurable.h"

// Definitions
#define ULT_TRIGGER_LOW_DELAY           4
#define ULT_TRIGGER_HIGH_DELAY          10
#define ULT_PING_TIMEOUT                6000
#define ULT_ROUNDTRIP_M                 5800.0f
#define ULT_ROUNDTRIP_CM                58
#define ULT_MAX_DISTANCE                100

#define ESP_ERR_ULTRASONIC_PING         0x200
#define ESP_ERR_ULTRASONIC_PING_TIMEOUT 0x201
#define ESP_ERR_ULTRASONIC_ECHO_TIMEOUT 0x202

// Class
class Ultrasonic : public Configuration::Configurable {

    Pin _trig_pin;
    Pin _echo_pin;

    int32_t _pause_time_ms = -1;        // Delay after pausing
    int32_t _pause_distance_cm = -1;    // Distance to trigger a pause

public:
	Ultrasonic();
    ~Ultrasonic();

    void init();
    bool is_active();
    bool within_pause_distance();
    int32_t get_pause_time_ms();

    // Configuration handlers.
    void validate() override;
    void group(Configuration::HandlerBase& handler) override;
    
protected:
    bool _is_active = false;

    esp_err_t measure_raw(uint32_t max_time_us, uint32_t *time_us);
    esp_err_t measure_m(float max_distance, float *distance);
    esp_err_t measure_cm(uint32_t max_distance, uint32_t *distance);
};
