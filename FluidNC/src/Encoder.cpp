// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#include "Encoder.h"

static const char *TAG = "encoder";

// Encoder constructor
Encoder::Encoder() {

    _is_active = false;
    _ready_flag = false;
	_pcnt_unit = PCNT_UNIT_0;
    _current_value = -1;
    _previous_value = -1;
    _difference = -1;
}

// Encoder destructor
Encoder::~Encoder() {}

// Encoder read task
void Encoder::read_task(void *pvParameters) {

    // Connect pointer
    Encoder* instance = static_cast<Encoder*>(pvParameters);

    // Loop forever
    while(1) {

        // Read new values when data has been read or not initialized
        if (!instance->_ready_flag) {

            // Save the previous value
            instance->_previous_value = instance->_current_value;

            // Read the current encoder value
            pcnt_get_counter_value(instance->_pcnt_unit, &instance->_current_value);

            // Calculate the difference
            instance->_difference = instance->_current_value - instance->_previous_value;

            // Set ready flag
            instance->_ready_flag = true;
        }

        // Check every 10ms
        vTaskDelay(ENC_READ_PERIODIC_MS/portTICK_PERIOD_MS);
    }
}

// Initializes the encoder subsystem
void Encoder::init() {

	pcnt_config_t pcnt_config;

    // Check if encoder pins configured
    if (!_a_pin.defined() || !_b_pin.defined()) {
        _is_active = false;
        return;
    }

    // Set up encoder A/B pins
    _a_pin.setAttr(Pin::Attr::PullUp);
    _b_pin.setAttr(Pin::Attr::PullUp);

    // Configure PCNT unit for encoder
    pcnt_config.pulse_gpio_num = _b_pin.getNative(Pin::Capabilities::Input);
    pcnt_config.ctrl_gpio_num = _a_pin.getNative(Pin::Capabilities::Input);
	pcnt_config.channel = PCNT_CHANNEL_0;
	pcnt_config.unit = _pcnt_unit;

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
	pcnt_set_filter_value(_pcnt_unit, 1023);
	pcnt_filter_enable(_pcnt_unit);

	// Initialize PCNT's counter
	pcnt_counter_pause(_pcnt_unit);
	pcnt_counter_clear(_pcnt_unit);

	// Everything is set up, now go to counting
	pcnt_counter_resume(_pcnt_unit);

    // Start read task
    xTaskCreate(read_task, "encoder_read_task", ENC_READ_STACK_SIZE, this, ENC_READ_PRIORITY, NULL);

    // Set flag
    _is_active = true;
}

// Get the difference between current and previous value
int16_t Encoder::get_difference() {

    // Return zero if not active or not ready yet
    if (!_is_active || !_ready_flag) return 0;

    // Return the difference and clear the flag
    int16_t difference = _difference;
    _ready_flag = false;

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
