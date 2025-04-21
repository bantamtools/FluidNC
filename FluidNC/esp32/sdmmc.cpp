#include "vfs_api.h"
#include "esp_vfs_fat.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "ff.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "../src/Machine/MachineConfig.h"

#include "Driver/sdmmc.h"
#include "src/Config.h"

#include <unordered_set>

#ifdef USE_SDMMC

// static const String allowed_file_ext[SD_NUM_ALLOWED_EXT] = {".gcode", ".nc", ".txt"};
// static const String allowed_binary_ext[SD_NUM_BIN_EXT] = {".bin"};

static const std::unordered_set<std::string>allowed_file_ext({".gcode", ".nc", ".txt"});
static const std::unordered_set<std::string>allowed_binary_ext({".bin"});
static const std::unordered_set<std::string>allowed_config_ext({".yaml"});

static bool sd_is_mounted = false;
static uint32_t _freq_hz = 20000000;

static esp_err_t mount_to_vfs_fat(int max_files, sdmmc_card_t* card, uint8_t pdrv, const char* base_path) {
    FATFS*    fs = NULL;
    esp_err_t err;
    ff_diskio_register_sdmmc(pdrv, card);

    //    ESP_LOGD(TAG, "using pdrv=%i", pdrv);
    // Drive names are "0:", "1:", etc.
    char drv[3] = { (char)('0' + pdrv), ':', 0 };

    FRESULT res;

    // connect FATFS to VFS
    err = esp_vfs_fat_register(base_path, drv, max_files, &fs);
    if (err == ESP_ERR_INVALID_STATE) {
        // it's okay, already registered with VFS
    } else if (err != ESP_OK) {
        //        ESP_LOGD(TAG, "esp_vfs_fat_register failed 0x(%x)", err);
        goto fail;
    }

    // Try to mount partition
    res = f_mount(fs, drv, 1);
    if (res != FR_OK) {
        err = ESP_FAIL;
        //        ESP_LOGW(TAG, "failed to mount card (%d)", res);
        goto fail;
    }
    return ESP_OK;

fail:
    if (fs) {
        f_mount(NULL, drv, 0);
    }
    esp_vfs_fat_unregister_path(base_path);
    ff_diskio_unregister(pdrv);
    return err;
}

sdmmc_host_t  host_config = SDMMC_HOST_DEFAULT();
sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
sdmmc_card_t* card        = NULL;
const char*   base_path   = "/sd";

static void call_host_deinit(const sdmmc_host_t* host_config) {
    if (host_config->flags & SDMMC_HOST_FLAG_DEINIT_ARG) {
        host_config->deinit_p(host_config->slot);
    } else {
        host_config->deinit();
    }
}

bool sd_init_slot(uint32_t freq_hz, int width, int clk_pin, int cmd_pin, int d0_pin, int d1_pin, int d2_pin, int d3_pin, int cd_pin) {

    esp_err_t err;

    // Set host frequency
    _freq_hz = freq_hz;
    host_config.max_freq_khz = _freq_hz / 1000;

    // Set bus width to use
    slot_config.width   = width;

    // Attach a set of GPIOs to the SD card slot
    slot_config.clk     = gpio_num_t(clk_pin);
    slot_config.cmd     = gpio_num_t(cmd_pin);
    slot_config.d0      = gpio_num_t(d0_pin);
    if (width == 4) {
        slot_config.d1      = gpio_num_t(d1_pin);
        slot_config.d2      = gpio_num_t(d2_pin);
        slot_config.d3      = gpio_num_t(d3_pin);
    }
    if (cd_pin > 0) {
        slot_config.cd  = gpio_num_t(cd_pin);
    }

    // Clear mount flag
    sd_is_mounted = false;

    return true;
}

std::error_code sd_mount(int max_files) {

    log_debug("SD Mount");

    esp_err_t err;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = max_files,
        .allocation_unit_size = 64 * 1024
    };

    // Bail if already mounted
    if (sd_is_mounted) {
        return std::error_code(ESP_OK, std::system_category());
    }

    // Mount SD card
    err = esp_vfs_fat_sdmmc_mount(base_path, &host_config, &slot_config, &mount_config, &card);

    // Empirically it is necessary to set the frequency twice.
    // If you do it only above, the max frequency will be pinned
    // at the highest "standard" frequency lower than the requested
    // one, which is 400 kHz for requested frequencies < 20 MHz.
    // If you do it only once below, the attempt to change it seems to
    // be ignored, and you get 20 MHz regardless of what you ask for.
    if (_freq_hz && (err == ESP_OK)) {
        err = sdmmc_host_set_card_clk(host_config.slot, _freq_hz / 1000);
    }

    // Set flag if mounted
    if (err == ESP_OK) {
        log_info("Mount_sd sdmmc successful");
        sd_is_mounted = true;
    }
    
    return std::error_code(err, std::system_category());
}

void sd_unmount() {

    log_debug("SD Unmount");
    esp_err_t err;

    // Unmount SD card if previously mounted
    if (sd_is_mounted) {

        err = esp_vfs_fat_sdcard_unmount(base_path, card);
        
        // Clear flag if unmounted
        if (err == ESP_OK) {
            log_info("Unmount_sd sdmmc");
            sd_is_mounted = false;
        }
    }
}

bool sd_card_is_present() {
    return sd_is_mounted;
}

void sd_populate_files_menu() {
    std::error_code ec;
    const std::filesystem::path fpath{base_path};
    char file_ext[LIST_NAME_MAX_PATH];
    char file_path[LIST_NAME_MAX_PATH];
    std::filesystem::file_time_type most_recent_time; // inits to zero epoch? May be platform dependent tho...
    char recent_file_path[LIST_NAME_MAX_PATH];
    recent_file_path[0] = '\0';

    // No display, bail
    if (!config->_oled) {
        return;
    }

    log_info("Populating Files Menu");

    // Clear the file list to start
    config->_oled->_menu->prep_for_sd_update();

    // Iterate through files if no errors (i.e. SD not found or corrupt)
    if (sd_is_mounted) {
        log_info("SD is mounted");
        // Iterate through the top level directory
        auto iter = std::filesystem::recursive_directory_iterator { fpath, ec };
        if (!ec) {
            std::filesystem::recursive_directory_iterator end;
            while (iter != end) {
                const auto& dir_entry = *iter;
                std::string filename = dir_entry.path().filename().string();

                // Skip hidden files and directories
                if (!filename.empty() && filename[0] == '.') {
                    if (dir_entry.is_directory()) {
                        iter.disable_recursion_pending();
                    }
                    ++iter; 
                    continue; // Skip cond
                }

                // Get the file extension and convert to lowercase
                std::string extension = dir_entry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

                bool added = false;

                // Check if the file extension is in the allowed file extensions
                if (allowed_file_ext.count(extension) > 0) {
                    std::string full_path = dir_entry.path().string();
                    std::string short_path = full_path.substr(strlen(base_path));

                    strncpy(file_path, short_path.c_str(), LIST_NAME_MAX_PATH);
                    added = config->_oled->_menu->add_sd_file(file_path, false); // Flag for adding G-code file

                    // Update most recent file if necessary
                    if (added && dir_entry.last_write_time() > most_recent_time) {
                        most_recent_time = dir_entry.last_write_time();
                        strncpy(recent_file_path, short_path.c_str(), LIST_NAME_MAX_PATH);
                    }
                }

                // Check if the file extension is in the allowed binary extensions
                else if (allowed_binary_ext.count(extension) > 0) {
                    std::string full_path = dir_entry.path().string();
                    std::string short_path = full_path.substr(strlen(base_path));

                    strncpy(file_path, short_path.c_str(), LIST_NAME_MAX_PATH);
                    config->_oled->_menu->add_sd_file(file_path, true); // Flag for adding binary file
                    //SAVE CONFIG PATH TO CONFIG
                }

                // Check if the file extension is in the allowed config extensions
                else if (allowed_config_ext.count(extension) > 0) {
                    std::string full_path = dir_entry.path().string();
                    std::string short_path = full_path.substr(strlen(base_path));

                    strncpy(file_path, short_path.c_str(), LIST_NAME_MAX_PATH);
                    config->_oled->_menu->add_sd_file(file_path, false, true); // Flag for adding config file
                    config->_recoveryConfig = full_path;
                }

                ++iter; // Advance the iterator
            }

            if(recent_file_path[0] != '\0'){
                config->_oled->_menu->set_recent_file(recent_file_path);
            } else {
                log_info("No Files Detected on SD");
            }
            
        }
    }

    // Refresh the menu
    config->_oled->refresh_display(true);
}

#endif
