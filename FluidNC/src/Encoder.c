// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#include "Encoder.h"

static const char *TAG = "encoder";

// A flag to identify if pcnt isr service has been installed.
static bool is_pcnt_isr_service_installed = false;
// A lock to avoid pcnt isr service being installed twice in multiple threads.
static _lock_t isr_service_install_lock;
#define LOCK_ACQUIRE() _lock_acquire(&isr_service_install_lock)
#define LOCK_RELEASE() _lock_release(&isr_service_install_lock)

// Encoder button press
volatile bool enc_btn_pressed = false;
static bool enc_btn_press_latched = false;

// Encoder unit and previous count
static rotary_encoder_t *encoder = NULL;
static int enc_cnt_prev = 0;

// Encoder button press handler
static void IRAM_ATTR enc_btn_handler(void *args)
{
    enc_btn_pressed = true;
}

static esp_err_t encoder_set_glitch_filter(rotary_encoder_t *encoder, uint32_t max_glitch_us)
{
    esp_err_t ret_code = ESP_OK;
    ec11_t *ec11 = __containerof(encoder, ec11_t, parent);

    /* Configure and enable the input filter */
    ROTARY_CHECK(pcnt_set_filter_value(ec11->pcnt_unit, max_glitch_us * 80) == ESP_OK, "set glitch filter failed", err, ESP_FAIL);

    if (max_glitch_us) {
        pcnt_filter_enable(ec11->pcnt_unit);
    } else {
        pcnt_filter_disable(ec11->pcnt_unit);
    }

    return ESP_OK;
err:
    return ret_code;
}

static esp_err_t encoder_start(rotary_encoder_t *encoder)
{
    ec11_t *ec11 = __containerof(encoder, ec11_t, parent);
    pcnt_counter_resume(ec11->pcnt_unit);
    return ESP_OK;
}

static esp_err_t encoder_stop(rotary_encoder_t *encoder)
{
    ec11_t *ec11 = __containerof(encoder, ec11_t, parent);
    pcnt_counter_pause(ec11->pcnt_unit);
    return ESP_OK;
}

static int encoder_get_counter_value(rotary_encoder_t *encoder)
{
    ec11_t *ec11 = __containerof(encoder, ec11_t, parent);
    int16_t val = 0;
    pcnt_get_counter_value(ec11->pcnt_unit, &val);
    return val + ec11->accumu_count;
}

static esp_err_t encoder_delete(rotary_encoder_t *encoder)
{
    ec11_t *ec11 = __containerof(encoder, ec11_t, parent);
    free(ec11);
    return ESP_OK;
}


static void encoder_pcnt_overflow_handler(void *arg)
{
    ec11_t *ec11 = (ec11_t *)arg;
    uint32_t status = 0;
    pcnt_get_event_status(ec11->pcnt_unit, &status);

    if (status & PCNT_EVT_H_LIM) {
        ec11->accumu_count += ENC_PCNT_HIGH_LIMIT;
    } else if (status & PCNT_EVT_L_LIM) {
        ec11->accumu_count += ENC_PCNT_LOW_LIMIT;
    }
}

esp_err_t encoder_create_new(const rotary_encoder_config_t *config, rotary_encoder_t **ret_encoder)
{
    esp_err_t ret_code = ESP_OK;
    ec11_t *ec11 = NULL;

    ROTARY_CHECK(config, "configuration can't be null", err, ESP_ERR_INVALID_ARG);
    ROTARY_CHECK(ret_encoder, "can't assign context to null", err, ESP_ERR_INVALID_ARG);

    ec11 = calloc(1, sizeof(ec11_t));
    ROTARY_CHECK(ec11, "allocate context memory failed", err, ESP_ERR_NO_MEM);

    ec11->pcnt_unit = (pcnt_unit_t)(config->dev);

    // Configure channel 0
    pcnt_config_t dev_config = {
        .pulse_gpio_num = config->phase_a_gpio_num,
        .ctrl_gpio_num = config->phase_b_gpio_num,
        .channel = PCNT_CHANNEL_0,
        .unit = ec11->pcnt_unit,
        .pos_mode = PCNT_COUNT_DEC,
        .neg_mode = PCNT_COUNT_DIS,                     // ms: using PCNT_COUNT_INC here doubles the count
        .lctrl_mode = PCNT_MODE_REVERSE,
        .hctrl_mode = PCNT_MODE_KEEP,
        .counter_h_lim = ENC_PCNT_HIGH_LIMIT,
        .counter_l_lim = ENC_PCNT_LOW_LIMIT,
    };
    ROTARY_CHECK(pcnt_unit_config(&dev_config) == ESP_OK, "config pcnt channel 0 failed", err, ESP_FAIL);

    // ms: disabled second channel, doubles the count again (1 turn = 4 counts)
    // Configure channel 1
    //dev_config.pulse_gpio_num = config->phase_b_gpio_num;
    //dev_config.ctrl_gpio_num = config->phase_a_gpio_num;
    //dev_config.channel = PCNT_CHANNEL_1;
    //dev_config.pos_mode = PCNT_COUNT_INC;
    //dev_config.neg_mode = PCNT_COUNT_DIS;
    //ROTARY_CHECK(pcnt_unit_config(&dev_config) == ESP_OK, "config pcnt channel 1 failed", err, ESP_FAIL);

    // PCNT pause and reset value
    pcnt_counter_pause(ec11->pcnt_unit);
    pcnt_counter_clear(ec11->pcnt_unit);


    // register interrupt handler in a thread-safe way
    LOCK_ACQUIRE();
    if (!is_pcnt_isr_service_installed) {
        ROTARY_CHECK(pcnt_isr_service_install(0) == ESP_OK, "install isr service failed", err, ESP_FAIL);
        // make sure pcnt isr service won't be installed more than one time
        is_pcnt_isr_service_installed = true;
    }
    LOCK_RELEASE();

    pcnt_isr_handler_add(ec11->pcnt_unit, encoder_pcnt_overflow_handler, ec11);

    pcnt_event_enable(ec11->pcnt_unit, PCNT_EVT_H_LIM);
    pcnt_event_enable(ec11->pcnt_unit, PCNT_EVT_L_LIM);

    ec11->parent.del = encoder_delete;
    ec11->parent.start = encoder_start;
    ec11->parent.stop = encoder_stop;
    ec11->parent.set_glitch_filter = encoder_set_glitch_filter;
    ec11->parent.get_counter_value = encoder_get_counter_value;

    *ret_encoder = &(ec11->parent);
    return ESP_OK;
err:
    if (ec11) {
        free(ec11);
    }
    return ret_code;
}

int16_t encoder_get_diff(void) {
/*
    int16_t enc_cnt_curr, enc_cnt_diff;

    // Get the current encoder count and calculate difference
    enc_cnt_curr = (int16_t)encoder_get_count();
    enc_cnt_diff = enc_cnt_curr - enc_cnt_prev;

    // Set previous count to current count
    enc_cnt_prev = enc_cnt_curr;

    // Return the difference
    return enc_cnt_diff;
*/
}

// Checks if the encoder button is pressed
bool encoder_is_pressed(void) {

    // Latched (debounced) button press, clear flag and return true
    if (enc_btn_press_latched) {

        // Clear flag
        enc_btn_press_latched = false;

        return true;
    }

    // No button press or bounce, return false
    return false;
}

// Initializes the encoder subsystem
void encoder_init(void) {
    
    uint32_t pcnt_unit = 0; // Encoder PCNT unit

    // Set up rotary encoder, filtering for 1us glitches and start
    rotary_encoder_config_t config = ROTARY_ENCODER_DEFAULT_CONFIG((rotary_encoder_dev_t)pcnt_unit, ENC_A_PIN, ENC_B_PIN);
    ESP_ERROR_CHECK(encoder_create_new(&config, &encoder));
    ESP_ERROR_CHECK(encoder->set_glitch_filter(encoder, 1));
    ESP_ERROR_CHECK(encoder->start(encoder));

    // Configure encoder (click wheel) button
    gpio_reset_pin(ENC_BTN_PIN); 
    gpio_set_direction(ENC_BTN_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(ENC_BTN_PIN, GPIO_PULLUP_ONLY);   

    // Read the initial count
    enc_cnt_prev = encoder_get_counter_value(encoder);

    // Fork a task for the encoder subsystem
    xTaskCreate(encoder_mgr, "encoder_mgr", ENC_MGR_STACK_SIZE, NULL, ENC_MGR_PRIORITY, NULL);
}

// Processes the encoder subsystem
void encoder_mgr(void *ptr) {

    // Loop forever
    while(1) {
        
        // Detected encoder button press, check for bounce and set latched value to true for LVGL
        if (enc_btn_pressed) {

            // Clear flag
            enc_btn_pressed = false;

            // Debounce 10ms in firmware
            vTaskDelay(10 /portTICK_PERIOD_MS);

            if (gpio_get_level(ENC_BTN_PIN) == 0) {

                enc_btn_press_latched = true;
            }
        }

        // Check every 10ms
        vTaskDelay(ENC_MGR_PERIODIC_MS/portTICK_PERIOD_MS);
    }
}
