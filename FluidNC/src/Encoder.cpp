// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#include "Encoder.h"

static const char *TAG = "encoder";

// Encoder constructor
Encoder::Encoder() {

	this->pcnt_unit = PCNT_UNIT_0;
}

// Encoder destructor
Encoder::~Encoder() {}

// Initializes the encoder subsystem
void Encoder::init() {

	pcnt_config_t pcnt_config;

    // Set up encoder A/B pins
    _a_pin.setAttr(Pin::Attr::PullUp);
    _b_pin.setAttr(Pin::Attr::PullUp);

    // Configure PCNT unit for encoder
    pcnt_config.pulse_gpio_num = _b_pin.getNative(Pin::Capabilities::Input);
    pcnt_config.ctrl_gpio_num = _a_pin.getNative(Pin::Capabilities::Input);
	pcnt_config.channel = PCNT_CHANNEL_0;
	pcnt_config.unit = this->pcnt_unit;

	// What to do on the positive / negative edge of pulse input?
	pcnt_config.pos_mode = PCNT_COUNT_DIS;      // Keep the counter value on the positive edge
	pcnt_config.neg_mode = PCNT_COUNT_INC;      // Count up on the negative edge

	// What to do when control input is low or high?
	pcnt_config.lctrl_mode = PCNT_MODE_KEEP;    // Keep the primary counter mode if low 
	pcnt_config.hctrl_mode = PCNT_MODE_REVERSE; // Reverse counting direction if high

	// Set the maximum and minimum limit values to watch
	pcnt_config.counter_h_lim = std::numeric_limits<int16_t>::max();
	pcnt_config.counter_l_lim = std::numeric_limits<int16_t>::min();  

	// Initialize PCNT unit
	pcnt_unit_config(&pcnt_config);

	// Configure and enable the input filter
	pcnt_set_filter_value(this->pcnt_unit, 1023);
	pcnt_filter_enable(this->pcnt_unit);

	// Initialize PCNT's counter
	pcnt_counter_pause(this->pcnt_unit);
	pcnt_counter_clear(this->pcnt_unit);

	// Everything is set up, now go to counting
	pcnt_counter_resume(this->pcnt_unit);

    // Set flag
    this->_is_active = true;

    // Store the current encoder value
	this->_previous_value = this->get_value();
}

// Get the current encoder value
int16_t Encoder::get_value() {
	int16_t value;

    // Return zero if not active
    if (!this->_is_active) return 0;

	pcnt_get_counter_value(this->pcnt_unit, &value);
	return value;
}

// Get the difference between current and previous value
int16_t Encoder::get_difference() {

    int16_t current_value, difference;

    // Return zero if not active
    if (!this->_is_active) return 0;

    // Get the current encoder count and calculate difference
    current_value = (int16_t)this->get_value();
    difference = current_value - this->_previous_value;

    // Set previous count to current count
    this->_previous_value = current_value;

    // Return the difference
    return difference;
}

// Returns active flag
bool Encoder::is_active() {
    return _is_active;
}

// Configurable functions
void Encoder::validate() {}

void Encoder::group(Configuration::HandlerBase& handler) {

    handler.item("a_pin", _a_pin);
    handler.item("b_pin", _b_pin);
}
