#pragma once

#include <system_error>
#include <filesystem>
#include "fluidnc_gpio.h"

#ifdef USE_SDMMC

// Definitions
#define SD_NUM_ALLOWED_EXT  3   // Number of allowed file extensions

bool sd_init_slot(uint32_t freq_hz, int width = 1, int clk_pin = -1, int cmd_pin = -1, int d0_pin = -1, int d1_pin = -1, int d2_pin = -1, int d3_pin = -1, int cd_pin = -1);
void sd_unmount();
void sd_deinit_slot();

std::error_code sd_mount(int max_files = 1);

bool sd_card_is_present();
void sd_populate_files_menu();

#endif
