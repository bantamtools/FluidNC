// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#include "Encoder.h"
#include "Machine/MachineConfig.h"

// Encoder constructor
Encoder::Encoder() {

	_pcnt_unit = PCNT_UNIT_0;
    _current_value = -1;
    _previous_value = -1;
    _difference = -1;
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
	pcnt_config.unit = _pcnt_unit;

	// What to do on the positive / negative edge of pulse input?
	pcnt_config.pos_mode = PCNT_COUNT_DEC;      // Count down on the positive edge
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

    // Load up the first current value, gives us an accurate difference on first read_task run
    pcnt_get_counter_value(_pcnt_unit, &_current_value);

    // Print configuration info message
    log_info("Encoder: A:" << _a_pin.name() << " B:" << _b_pin.name());
}

// Read the encoder value and calculate difference
void Encoder::read() {

    // Save the previous value
    _previous_value = _current_value;

    // Read the current encoder value
    pcnt_get_counter_value(_pcnt_unit, &_current_value);

    // Calculate the difference
    _difference = _current_value - _previous_value;

    // Trigger screen update on change if IDLE
    if ((_difference != 0) && (sys.state == State::Idle)) {
        config->_oled->process_encoder(_difference);
    }
}

// Configurable functions
void Encoder::validate() {

    if (!_a_pin.undefined() || !_b_pin.undefined()) {
        Assert(!_a_pin.undefined(), "Encoder A pin should be configured.");
        Assert(!_b_pin.undefined(), "Encoder B pin should be configured.");
    }
}

void Encoder::group(Configuration::HandlerBase& handler) {

    handler.item("a_pin", _a_pin);
    handler.item("b_pin", _b_pin);
}
