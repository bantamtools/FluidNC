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
 * @file Ultrasonic.cpp
 *
 * ESP-IDF driver for ultrasonic range meters, e.g. HC-SR04, HY-SRF05 and the like
 *
 * Ported from esp-open-rtos
 *
 * Copyright (c) 2016 Ruslan V. Uss <unclerus@gmail.com>
 *
 * BSD Licensed as described in the file LICENSE at https://github.com/UncleRus/esp-idf-lib/tree/master/components/ultrasonic
 */

#include "Ultrasonic.h"

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
#define PORT_ENTER_CRITICAL portENTER_CRITICAL(&mux)
#define PORT_EXIT_CRITICAL portEXIT_CRITICAL(&mux)

#define timeout_expired(start, len) ((esp_timer_get_time() - (start)) >= (len))

#define CHECK_ARG(VAL) do { if (!(VAL)) return ESP_ERR_INVALID_ARG; } while (0)
#define CHECK(x) do { esp_err_t __; if ((__ = x) != ESP_OK) return __; } while (0)
#define RETURN_CRITICAL(RES) do { PORT_EXIT_CRITICAL; return RES; } while(0)

// Ultraonic constructor
Ultrasonic::Ultrasonic() {}

// Ultrasonic destructor
Ultrasonic::~Ultrasonic() {}

/**
 * @brief Initializes the ultrasonic subsystem
 *
 * @return none
 */
void Ultrasonic::init() {

    // Check if ultrasonic sensor configured
    if (!_trig_pin.defined() || !_echo_pin.defined() || (_pause_time_ms < 0) || (_pause_distance_cm < 0)) {
        _is_active = false;
        return;
    }

    // Set up ultrasonic trigger and echo pins
    _trig_pin.setAttr(Pin::Attr::Output);
    _echo_pin.setAttr(Pin::Attr::Input);

    _trig_pin.write(0);

    // Set flag
    _is_active = true;
}

// Returns active flag
bool Ultrasonic::is_active() {
    return _is_active;
}

/**
 * @brief Measure time between ping and echo
 *
 * @param max_time_us Maximal time to wait for echo
 * @param[out] time_us Time, us
 * @return `ESP_OK` on success, otherwise:
 *         - ::ESP_ERR_ULTRASONIC_PING         - Invalid state (previous ping is not ended)
 *         - ::ESP_ERR_ULTRASONIC_PING_TIMEOUT - Device is not responding
 *         - ::ESP_ERR_ULTRASONIC_ECHO_TIMEOUT - Distance is too big or wave is scattered
 */
esp_err_t Ultrasonic::measure_raw(uint32_t max_time_us, uint32_t *time_us) {

    CHECK_ARG(time_us);

    PORT_ENTER_CRITICAL;

    // Ping: Low for 2..4 us, then high 10 us
    _trig_pin.write(0);
    esp_rom_delay_us(ULT_TRIGGER_LOW_DELAY);
    _trig_pin.write(1);
    esp_rom_delay_us(ULT_TRIGGER_HIGH_DELAY);
    _trig_pin.write(0);

    // Previous ping isn't ended
    if (_echo_pin.read())
        RETURN_CRITICAL(ESP_ERR_ULTRASONIC_PING);

    // Wait for echo
    int64_t start = esp_timer_get_time();
    while (!_echo_pin.read())
    {
        if (timeout_expired(start, ULT_PING_TIMEOUT))
            RETURN_CRITICAL(ESP_ERR_ULTRASONIC_PING_TIMEOUT);
    }

    // got echo, measuring
    int64_t echo_start = esp_timer_get_time();
    int64_t time = echo_start;
    while (_echo_pin.read())
    {
        time = esp_timer_get_time();
        if (timeout_expired(echo_start, max_time_us))
            RETURN_CRITICAL(ESP_ERR_ULTRASONIC_ECHO_TIMEOUT);
    }
    PORT_EXIT_CRITICAL;

    *time_us = time - echo_start;

    return ESP_OK;
}

/**
 * @brief Measure distance in meters
 *
 * @param max_distance Maximal distance to measure, meters
 * @param[out] distance Distance in meters
 * @return `ESP_OK` on success, otherwise:
 *         - ::ESP_ERR_ULTRASONIC_PING         - Invalid state (previous ping is not ended)
 *         - ::ESP_ERR_ULTRASONIC_PING_TIMEOUT - Device is not responding
 *         - ::ESP_ERR_ULTRASONIC_ECHO_TIMEOUT - Distance is too big or wave is scattered
 */
esp_err_t Ultrasonic::measure_m(float max_distance, float *distance) {

    CHECK_ARG(distance);

    uint32_t time_us;
    CHECK(measure_raw(max_distance * ULT_ROUNDTRIP_M, &time_us));
    *distance = time_us / ULT_ROUNDTRIP_M;

    return ESP_OK;
}

/**
 * @brief Measure distance in centimeters
 *
 * @param max_distance Maximal distance to measure, centimeters
 * @param[out] distance Distance in centimeters
 * @return `ESP_OK` on success, otherwise:
 *         - ::ESP_ERR_ULTRASONIC_PING         - Invalid state (previous ping is not ended)
 *         - ::ESP_ERR_ULTRASONIC_PING_TIMEOUT - Device is not responding
 *         - ::ESP_ERR_ULTRASONIC_ECHO_TIMEOUT - Distance is too big or wave is scattered
 */
esp_err_t Ultrasonic::measure_cm(uint32_t max_distance, uint32_t *distance) {

    CHECK_ARG(distance);

    uint32_t time_us;
    CHECK(measure_raw(max_distance * ULT_ROUNDTRIP_CM, &time_us));
    *distance = time_us / ULT_ROUNDTRIP_CM;

    return ESP_OK;
}

// Checks whether we are within the pause distance or not
bool Ultrasonic::within_pause_distance(void) {

    // Return if no distance set
    if (_pause_distance_cm < 0) return false;

    uint32_t dist_cm;
    CHECK(measure_cm(ULT_MAX_DISTANCE, &dist_cm));

    return (dist_cm <= _pause_distance_cm);
}

// Returns the configured pause time in milliseconds
int32_t Ultrasonic::get_pause_time_ms(void) {
    return _pause_time_ms;
}

// Configurable functions
void Ultrasonic::validate() {}

void Ultrasonic::group(Configuration::HandlerBase& handler) {

    handler.item("trig_pin", _trig_pin);
    handler.item("echo_pin", _echo_pin);
    handler.item("pause_time_ms", _pause_time_ms);
    handler.item("pause_distance_cm", _pause_distance_cm);
}
