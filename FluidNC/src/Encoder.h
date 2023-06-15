// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#ifndef ENCODER_H
#define ENCODER_H

#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_compiler.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/pcnt.h"
#include "sys/lock.h"
#include "hal/pcnt_hal.h"

// Definitions
#define ENC_A_PIN                   35
#define ENC_B_PIN                   48
#define ENC_BTN_PIN                 36

#define ENC_PCNT_HIGH_LIMIT         1000
#define ENC_PCNT_LOW_LIMIT          -1000
#define ENC_PCNT_GLITCH_NS          10000

#define ENCODER_MGR_PERIODIC_MS     10

#define ROTARY_ENCODER_DEFAULT_CONFIG(dev_hdl, gpio_a, gpio_b) \
    {                                                          \
        .dev = dev_hdl,                                        \
        .phase_a_gpio_num = gpio_a,                            \
        .phase_b_gpio_num = gpio_b,                            \
        .flags = 0,                                            \
    }

#define ROTARY_CHECK(a, msg, tag, ret, ...)                                       \
    do {                                                                          \
        if (unlikely(!(a))) {                                                     \
            ESP_LOGE(TAG, "%s(%d): " msg, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            ret_code = ret;                                                       \
            goto tag;                                                             \
        }                                                                         \
    } while (0)

// Type Definitions
typedef void *rotary_encoder_dev_t;

typedef struct {
    rotary_encoder_dev_t dev;
    int phase_a_gpio_num;
    int phase_b_gpio_num;
    int flags;
} rotary_encoder_config_t;

typedef struct rotary_encoder_t rotary_encoder_t;

struct rotary_encoder_t {

    esp_err_t (*set_glitch_filter)(rotary_encoder_t *encoder, uint32_t max_glitch_us);
    esp_err_t (*start)(rotary_encoder_t *encoder);
    esp_err_t (*stop)(rotary_encoder_t *encoder);
    esp_err_t (*del)(rotary_encoder_t *encoder);
    int (*get_counter_value)(rotary_encoder_t *encoder);
};

typedef struct {
    int accumu_count;
    rotary_encoder_t parent;
    pcnt_unit_t pcnt_unit;
} ec11_t;

// Function Prototypes
int encoder_get_count(void);
int16_t encoder_get_diff(void);
bool encoder_is_pressed(void);
void encoder_init(void);
void encoder_mgr(void*);

#endif