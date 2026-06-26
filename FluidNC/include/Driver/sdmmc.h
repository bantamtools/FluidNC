#pragma once

#include <system_error>
#include <filesystem>
#include "fluidnc_gpio.h"

#ifdef USE_SDMMC

// Definitions
#define SD_NUM_ALLOWED_EXT  2   // Number of allowed file extensions
#define SD_NUM_BIN_EXT 1        // Number of allowed file extensions for binaries (.bin)
#define SD_NUM_CFG_EXT 1        // Number of allowed file extensions for config files (.yaml)

bool sd_init_slot(uint32_t freq_hz, int width = 1, int clk_pin = -1, int cmd_pin = -1, int d0_pin = -1, int d1_pin = -1, int d2_pin = -1, int d3_pin = -1, int cd_pin = -1);
void sd_unmount();
void sd_deinit_slot();

std::error_code sd_mount(int max_files = 1);

bool sd_card_is_present();
// Records physical card presence as signaled by the card-detect pin.
// Call with present=true on card insertion, false on removal.
// Not called on boards with no card-detect pin; those boards always attempt the mount.
void sd_set_card_present(bool present);
void sd_populate_files_menu();

// Incremental SD menu cache mutations. See FluidNC/src/SDMenuEvents.h
// for the full contract. No-op on non-"/sd/" paths and on hosts
// without OLED; safe to call unconditionally.
void sd_files_added(const char* full_path);
void sd_files_removed(const char* full_path, bool was_directory);
void sd_files_renamed(const char* old_full_path,
                      const char* new_full_path);

#endif
