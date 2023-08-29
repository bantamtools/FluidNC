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
#include "Machine/MachineConfig.h"

// Ultraonic constructor
Ultrasonic::Ultrasonic() {}

// Ultrasonic destructor
Ultrasonic::~Ultrasonic() {}

// Initializes the ultrasonic subsystem
void Ultrasonic::init() {

    // Set up ultrasonic trigger and echo pins
    _trig_pin.setAttr(Pin::Attr::Output);
    _echo_pin.setAttr(Pin::Attr::Input);
    _trig_pin.write(0);

    // Print configuration info message
    log_info("Ultrasonic:" << " TRIG:" << _trig_pin.name() << " ECHO:" << _echo_pin.name() << 
        " pause_time:" << _pause_time_ms << "ms" <<
        " pause_distance:" << _pause_distance_cm << "cm");

}

// Measure time between ping and echo
esp_err_t Ultrasonic::measure_raw(uint32_t max_time_us, uint32_t *time_us) {

    if (!time_us) {
        return ESP_ERR_INVALID_ARG;
    }

    // Ping: Low for 2..4 us, then high 10 us
    _trig_pin.write(0);
    esp_rom_delay_us(ULT_TRIGGER_LOW_DELAY);
    _trig_pin.write(1);
    esp_rom_delay_us(ULT_TRIGGER_HIGH_DELAY);
    _trig_pin.write(0);

    // Previous ping isn't ended
    if (_echo_pin.read()) {
        return ESP_ERR_ULTRASONIC_PING;
    }

    // Wait for echo
    int64_t start = esp_timer_get_time();
    while (!_echo_pin.read())
    {
        if ((esp_timer_get_time() - start) >= ULT_PING_TIMEOUT) {
            return ESP_ERR_ULTRASONIC_PING_TIMEOUT;
        }
    }

    // got echo, measuring
    int64_t echo_start = esp_timer_get_time();
    int64_t time = echo_start;

    while (_echo_pin.read())
    {
        time = esp_timer_get_time();
        if ((esp_timer_get_time() - echo_start) >= max_time_us) {
            return ESP_ERR_ULTRASONIC_ECHO_TIMEOUT;
        }
    }

    *time_us = time - echo_start;

    return ESP_OK;
}

// Measure distance in meters
esp_err_t Ultrasonic::measure_m(float max_distance, float *distance) {

    esp_err_t ret;

    if (!distance) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t time_us;
    if ((ret = measure_raw(max_distance * ULT_ROUNDTRIP_M, &time_us)) != ESP_OK) {
        return ret;
    }
    *distance = time_us / ULT_ROUNDTRIP_M;

    return ESP_OK;
}

// Measure distance in centimeters
esp_err_t Ultrasonic::measure_cm(uint32_t max_distance, uint32_t *distance) {

    esp_err_t ret;

    if (!distance) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t time_us;
    if ((ret = measure_raw(max_distance * ULT_ROUNDTRIP_CM, &time_us)) != ESP_OK) {
        return ret;
    }
    *distance = time_us / ULT_ROUNDTRIP_CM;

    return ESP_OK;
}

// Checks whether we are within the pause distance or not
bool Ultrasonic::within_pause_distance(void) {

    // Return if no distance set
    if (_pause_distance_cm < 0) return false;

    return (_dist_cm <= _pause_distance_cm);
}

// Reads the ultrasonic sensor
void Ultrasonic::read() {

    uint32_t dist_cm;

    // Read the distance and save locally if valid
    // or set to max distance if error
    if (measure_cm(ULT_MAX_DISTANCE, &dist_cm) == ESP_OK) {
        _dist_cm = dist_cm;
    } else {
        _dist_cm = ULT_MAX_DISTANCE;
    }

    // Process the ultrasonic reading based on the current state
    switch (sys.state) {

        // Feedhold during a cycle if we're within the pause distance and start timer
        case State::Cycle:

            if (within_pause_distance()) {
                protocol_send_event(&feedHoldEvent);
                _pause_active = true;
            }
            break;

        // Resume from feedhold after a pause
        case State::Hold:

            // Once in HOLD, schedule pause for specified time unless already scheduled
            if (_pause_active && _pause_end_time == 0) {
                _pause_end_time = usToEndTicks(_pause_time_ms * 1000);
                // _pause_end_time 0 means that a resume is not scheduled. so if we happen to
                // land on 0 as an end time, just push it back by one microsecond to get off 0.
                if (_pause_end_time == 0) {
                    _pause_end_time = 1;
                }

            // Check to see if we should resume from feedhold
            // If _pause_end_time is 0, no pause is pending.
            } else if (_pause_active && _pause_end_time && (getCpuTicks() - _pause_end_time) > 0) {
                _pause_end_time = 0;

                // Still have an object in the way, restart the timer
                if (within_pause_distance()) {
                    _pause_end_time = usToEndTicks(_pause_time_ms * 1000);
                    // _pause_end_time 0 means that a resume is not scheduled. so if we happen to
                    // land on 0 as an end time, just push it back by one microsecond to get off 0.
                    if (_pause_end_time == 0) {
                        _pause_end_time = 1;
                    }
                    
                // Otherwise, resume motion
                } else {
                    _pause_active = false;
                    protocol_send_event(&cycleStartEvent);
                }
            }
            break;

        default: break;
    }
}

// Configurable functions
void Ultrasonic::validate() {

    if (!_trig_pin.undefined() || !_echo_pin.undefined() || (_pause_time_ms < 0) || (_pause_distance_cm < 0)) {
        Assert(!_trig_pin.undefined(), "Ultrasonic TRIG pin should be configured.");
        Assert(!_echo_pin.undefined(), "Ultrasonic ECHO pin should be configured.");
        Assert((_pause_time_ms >= 0), "Ultrasonic pause time should be positive.");
        Assert(((_pause_distance_cm >= 0) && (_pause_distance_cm <= ULT_MAX_DISTANCE)), "Ultrasonic pause distance should be between 0-100cm.");
    }
}

void Ultrasonic::group(Configuration::HandlerBase& handler) {

    handler.item("trig_pin", _trig_pin);
    handler.item("echo_pin", _echo_pin);
    handler.item("pause_time_ms", _pause_time_ms);
    handler.item("pause_distance_cm", _pause_distance_cm);
}
