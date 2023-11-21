// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#include "Encoder.h"
#include "Machine/MachineConfig.h"

static const char *TAG = "encoder";

// Encoder constructor
Encoder::Encoder() {

	_pcnt_unit = PCNT_UNIT_0;
    _difference = 0;
}

// Encoder destructor
Encoder::~Encoder() {}

// Encoder read callback
void IRAM_ATTR Encoder::encoder_read_cb(void *args) {

    // Connect pointer
    Encoder* instance = static_cast<Encoder*>(args);

    // Read the current encoder value
    pcnt_get_counter_value(instance->_pcnt_unit, &instance->_difference);

    // Clear the counter to zero
    pcnt_counter_clear(instance->_pcnt_unit);
}

// Initializes the encoder subsystem
void Encoder::init() {

	pcnt_config_t pcnt_config;

    // Encoder not configured, use MVP as fail-safe default
    if (!_a_pin.defined() && !_b_pin.defined()) {
        _a_pin = Pin::create("gpio.35");
        _b_pin = Pin::create("gpio.48");
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

    // Set interruipt events for +/-1
    pcnt_set_event_value(_pcnt_unit, PCNT_EVT_THRES_1, 1);
    pcnt_event_enable(_pcnt_unit, PCNT_EVT_THRES_1);
    pcnt_set_event_value(_pcnt_unit, PCNT_EVT_THRES_0, -1);
    pcnt_event_enable(_pcnt_unit, PCNT_EVT_THRES_0);

	// Initialize PCNT's counter
	pcnt_counter_pause(_pcnt_unit);
	pcnt_counter_clear(_pcnt_unit);

	// Everything is set up, now go to counting
	pcnt_counter_resume(_pcnt_unit);

    // Install the PCNT ISR service and add read callback
    pcnt_isr_service_install(0);
    pcnt_isr_handler_add(_pcnt_unit, encoder_read_cb, this);    
}

// Get the difference between current and previous value
int16_t Encoder::get_difference() {

    // Return the difference and reset internal
    int16_t difference = _difference;
    _difference = 0;

    return difference;
}

// Configurable functions
void Encoder::validate() {
    if (_a_pin.defined() || _b_pin.defined()) {
        Assert(_a_pin.defined(), "A pin should be configured once");
        Assert(_b_pin.defined(), "B pin should be configured once");
    }
}

void Encoder::group(Configuration::HandlerBase& handler) {

    handler.item("a_pin", _a_pin);
    handler.item("b_pin", _b_pin);
}
