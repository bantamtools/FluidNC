// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#include "Encoder.h"

static const char *TAG = "encoder";

// Encoder button press handler
void IRAM_ATTR Encoder::btn_handler(void *args) {

    this->enc_btn_pressed = true;
}

// Encoder constructor
Encoder::Encoder(gpio_num_t a_pin, gpio_num_t b_pin, pcnt_unit_t pcnt_unit) {

	this->a_pin = a_pin;
	this->b_pin = b_pin;
	this->pcnt_unit = pcnt_unit;

    this->enc_btn_pressed = false;
    this->enc_btn_press_latched = false;
}

// Initializes the encoder subsystem
void Encoder::init() {

	gpio_set_pull_mode(this->a_pin, GPIO_PULLUP_ONLY);
	gpio_set_pull_mode(this->b_pin, GPIO_PULLUP_ONLY);

	/* Prepare configuration for the PCNT unit */
	pcnt_config_t pcnt_config;

	pcnt_config.pulse_gpio_num = this->a_pin;
	pcnt_config.ctrl_gpio_num = this->b_pin;
	pcnt_config.channel = PCNT_CHANNEL_0;
	pcnt_config.unit = pcnt_unit;

	// What to do on the positive / negative edge of pulse input?
	pcnt_config.pos_mode = PCNT_COUNT_DIS;      // Keep the counter value on the positive edge
	pcnt_config.neg_mode = PCNT_COUNT_INC;      // Count up on the negative edge

	// What to do when control input is low or high?
	pcnt_config.lctrl_mode = PCNT_MODE_KEEP;    // Keep the primary counter mode if low 
	pcnt_config.hctrl_mode = PCNT_MODE_REVERSE; // Reverse counting direction if high

	// Set the maximum and minimum limit values to watch
	pcnt_config.counter_h_lim = std::numeric_limits<int16_t>::max();
	pcnt_config.counter_l_lim = std::numeric_limits<int16_t>::min();  

	/* Initialize PCNT unit */
	pcnt_unit_config(&pcnt_config);

	/* Configure and enable the input filter */
	pcnt_set_filter_value(this->pcnt_unit, 1023);
	pcnt_filter_enable(this->pcnt_unit);

	/* Enable events on zero, maximum and minimum limit values */
	//pcnt_event_enable(this->pcntUnit, PCNT_EVT_ZERO);
	//pcnt_event_enable(this->pcntUnit, PCNT_EVT_H_LIM);
	//pcnt_event_enable(this->pcntUnit, PCNT_EVT_L_LIM);

	/* Initialize PCNT's counter */
	pcnt_counter_pause(this->pcnt_unit);
	pcnt_counter_clear(this->pcnt_unit);

	/* Register ISR handler and enable interrupts for PCNT unit */
	//pcnt_isr_register(pcnt_example_intr_handler, NULL, 0, &user_isr_handle);
	//pcnt_intr_enable(PCNT_TEST_UNIT);

	/* Everything is set up, now go to counting */
	pcnt_counter_resume(this->pcnt_unit);

    // Store the current encoder value
	this->previous_value = this->get_value();

    // Fork a task for the encoder subsystem
    //xTaskCreate(this->mgr, "encoder_mgr", ENC_MGR_STACK_SIZE, NULL, ENC_MGR_PRIORITY, NULL);
}

// Get the current encoder value
int16_t Encoder::get_value() {
	int16_t value;
	pcnt_get_counter_value(this->pcnt_unit, &value);
	return value;
}

// Get the difference between current and previous value
int16_t Encoder::get_difference() {

    int16_t current_value, difference;

    // Get the current encoder count and calculate difference
    current_value = (int16_t)this->get_value();
    difference = current_value - this->previous_value;

    // Set previous count to current count
    this->previous_value = current_value;

    // Return the difference
    return difference;
}

// Checks if the encoder button is pressed
bool Encoder::is_pressed(void) {

    // Latched (debounced) button press, clear flag and return true
    if (this->enc_btn_press_latched) {

        // Clear flag
        this->enc_btn_press_latched = false;

        return true;
    }

    // No button press or bounce, return false
    return false;
}

/*
int32_t Encoder::getAddition() const {
	auto A = gpio_get_level(this->gpioPinA);
	auto B = gpio_get_level(this->gpioPinB);
	int32_t addition = (B << 1) + (A ^ B);
	return addition;
}*/

// Processes the encoder subsystem
void Encoder::mgr(void *ptr) {

    // Loop forever
    while(1) {
        
        // Detected encoder button press, check for bounce and set latched value to true for LVGL
        if (this->enc_btn_pressed) {

            // Clear flag
            this->enc_btn_pressed = false;

            // Debounce 10ms in firmware
            vTaskDelay(10 /portTICK_PERIOD_MS);

            if (gpio_get_level(ENC_BTN_PIN) == 0) {

                this->enc_btn_press_latched = true;
            }
        }

        // Check every 10ms
        vTaskDelay(ENC_MGR_PERIODIC_MS/portTICK_PERIOD_MS);
    }
}