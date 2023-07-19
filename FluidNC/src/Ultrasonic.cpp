// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#include "Ultrasonic.h"

static const char *TAG = "ultrasonic";

// Ultraonic constructor
Ultrasonic::Ultrasonic() {}

// Ultrasonic destructor
Ultrasonic::~Ultrasonic() {}

// Initializes the ultrasonic subsystem
void Ultrasonic::init() {

    // Check pins, settings
    //TODO
}

// Configurable functions
void Ultrasonic::validate() {}

void Ultrasonic::group(Configuration::HandlerBase& handler) {

    handler.item("trig_pin", _trig_pin);
    handler.item("echo_pin", _echo_pin);
    handler.item("pause_time_ms", _pause_time_ms);
    handler.item("pause_distance_cm", _pause_distance_cm);
}
