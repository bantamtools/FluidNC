#pragma once

#include <system_error>
#include <filesystem>
#include "fluidnc_gpio.h"

#ifndef USE_SDMMC

// Definitions
#define SD_NUM_ALLOWED_EXT  3   // Number of allowed file extensions

bool sd_init_slot(uint32_t freq_hz, int cs_pin, int cd_pin = -1, int wp_pin = -1);
void sd_unmount();
void sd_deinit_slot();

std::error_code sd_mount(int max_files = 1);

bool sd_card_is_present();
void sd_populate_files_menu();

// Incremental SD menu cache mutations. See FluidNC/src/SDMenuEvents.h
// for the full contract. No-op on non-"/sd/" paths and on hosts
// without OLED; safe to call unconditionally.
void sd_files_added(const char* full_path);
void sd_files_removed(const char* full_path, bool was_directory);
void sd_files_renamed(const char* old_full_path,
                      const char* new_full_path);

#endif
