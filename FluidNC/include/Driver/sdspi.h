#pragma once

#include <system_error>
#include <filesystem>
#include "fluidnc_gpio.h"

// Definitions
#define SD_MAX_STR          255 // Max string length for paths
#define SD_MAX_FILES        100 // Max number of files in a directory listing
#define SD_NUM_ALLOWED_EXT  3   // Number of allowed file extensions

// Type Definitions
typedef struct {
    char path[SD_MAX_FILES][SD_MAX_STR];
    int num_files;
} FileListType;

bool sd_init_slot(uint32_t freq_hz, int cs_pin, int cd_pin = -1, int wp_pin = -1);
void sd_unmount();
void sd_deinit_slot();

std::error_code sd_mount(int max_files = 1);

void sd_list_files();
FileListType *sd_get_filelist(void);
