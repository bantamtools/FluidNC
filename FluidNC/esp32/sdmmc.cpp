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
#include "../src/SDMenuEvents.h"

#include <unordered_set>
#include <filesystem>
#include <system_error>
#include <cstring>

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

    // Maximum number of files to load from SD card to prevent memory exhaustion
    const int MAX_SD_FILES = 1000;
    uint32_t file_count = 0;
    bool limit_reached = false;
    bool scan_error = false;

    // No display, bail
    if (!config->_oled) {
        return;
    }

    log_info("Populating Files Menu");

    // Clear the file list to start
    config->_oled->_menu->prep_for_sd_update();

    // Only scan if card is actually mounted
    if (!sd_is_mounted) {
        log_info("SD card not mounted, skipping file scan");
        config->_oled->_menu->finish_sd_update();
        config->_oled->refresh_display(true);
        return;
    }

    // Iterate through files if no errors (i.e. SD not found or corrupt)
    if (sd_is_mounted) {
        log_info("SD is mounted");
        try {
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
                        if (added) {
                            file_count++;
                        }

                        // Update most recent file if necessary
                        if (added) {
                            try {
                                auto file_time = dir_entry.last_write_time();
                                if (file_time > most_recent_time) {
                                    most_recent_time = file_time;
                                    strncpy(recent_file_path, short_path.c_str(), LIST_NAME_MAX_PATH);
                                }
                            } catch (...) {
                                // SD card I/O error during file time access - skip this file
                                log_warn("Failed to get file time for: " << short_path);
                            }
                        }

                        // Log heap status and check memory limit every 20 files
                        if (file_count % 20 == 0) {
                            uint32_t current_heap = ESP.getFreeHeap();
                            log_info("Files read: " << file_count << ", Heap: " << current_heap << " bytes");

                            // Show loading progress (starting at 40 files)
                            if (file_count > 39) {
                                char msg[55];
                                snprintf(msg, sizeof(msg), "microSD Card:\nReading %d files...", file_count);
                                config->_oled->popup_msg(msg, 0);
                                config->_oled->processDisplayRefresh(); // Force display update
                            }

                            // Stop if heap drops below safe threshold (50 kB)
                            // WiFi stack is already allocated by scan time;
                            // 50 kB reserves room for HTTP/telnet transients,
                            // RSS fetches, filesystem iterators, FreeRTOS overhead,
                            // and heap fragmentation from many small file entries.
                            if (current_heap < 50000) {
                                log_warn("Memory limit reached: " << file_count << " files read; stopping scan (heap: " << current_heap << " bytes)");
                                config->_oled->clear_popup();  // Clear loading message first
                                char msg[55];
                                snprintf(msg, sizeof(msg), "File limit reached:\nRead %d files from microSD.", file_count);
                                config->_oled->popup_msg(msg, 0);
                                limit_reached = true;
                                break;
                            }
                        }
                    }

                    // Check if the file extension is in the allowed binary extensions
                    else if (allowed_binary_ext.count(extension) > 0) {
                        std::string full_path = dir_entry.path().string();
                        std::string short_path = full_path.substr(strlen(base_path));

                        strncpy(file_path, short_path.c_str(), LIST_NAME_MAX_PATH);
                        if (config->_oled->_menu->add_sd_file(file_path, true)) { // Flag for adding binary file
                            file_count++;
                        }
                        //SAVE CONFIG PATH TO CONFIG
                    }

                    // Check if the file extension is in the allowed config extensions
                    else if (allowed_config_ext.count(extension) > 0) {
                        std::string full_path = dir_entry.path().string();
                        std::string short_path = full_path.substr(strlen(base_path));

                        strncpy(file_path, short_path.c_str(), LIST_NAME_MAX_PATH);
                        if (config->_oled->_menu->add_sd_file(file_path, false, true)) { // Flag for adding config file
                            file_count++;
                        }
                    }

                    // Check if we've reached the absolute file limit
                    if (file_count >= MAX_SD_FILES) {
                        log_warn("Absolute file limit reached: " << file_count << " files read, stopping scan");
                        config->_oled->clear_popup();  // Clear loading message first
                        char msg[55];
                        snprintf(msg, sizeof(msg), "File limit reached:\nRead %d files from microSD.", file_count);
                        config->_oled->popup_msg(msg, 0);
                        limit_reached = true;
                        break;
                    }

                    ++iter; // Advance the iterator
                }

                if(recent_file_path[0] != '\0'){
                    config->_oled->_menu->set_recent_file(recent_file_path);
                } else {
                    log_info("No Files Detected on SD");
                }

            }
        } catch (const std::exception& e) {
            std::string err_msg = e.what();
            // Extract just the error type from verbose filesystem error messages
            if (err_msg.find("Bad file number") != std::string::npos) {
                log_error("microSD I/O error. Please restart machine. [bad file number]");
            } else {
                log_error("microSD I/O error. Please restart machine. [" << err_msg << "]");
            }
            scan_error = true;
        } catch (...) {
            log_error("microSD I/O error. Please restart machine. [unknown error]");
            scan_error = true;
        }
    }

    // Report final file count
    log_info("File scan complete: " << file_count << " files read");
    log_debug("Total file_count = " << file_count);

    // Clear loading progress message (unless we hit a limit or error and are showing that message)
    if (!limit_reached && !scan_error) {
        config->_oled->clear_popup();
    }

    // Log final memory usage after all files loaded
    config->_oled->_menu->finish_sd_update();

    // Refresh the menu
    config->_oled->refresh_display(true);
}

// Incremental SD menu cache mutations. See FluidNC/src/SDMenuEvents.h.
// These exist so user-initiated single-file mutations don't pay the
// 10s+ user-interrupting full-rescan cost of sd_populate_files_menu().
//
// Covers all three menu classes the populate function handles:
// gcode (.gcode/.nc/.txt) -> _files_menu, binary (.bin) ->
// _firmware_menu, config (.yaml) -> _config_menu. Anything outside
// those classes is filtered (no menu entry to add or remove).

// Map a FileClass to add_sd_file's (isBin, isCfg) flag pair.
// Mirrors the dispatch in sd_populate_files_menu above.
static bool add_sd_file_for_class(SDMenuEvents::FileClass cls,
                                  char* path) {
    using SDMenuEvents::FileClass;
    switch (cls) {
        case FileClass::Gcode:
            return config->_oled->_menu->add_sd_file(path, false, false);
        case FileClass::Bin:
            return config->_oled->_menu->add_sd_file(path, true,  false);
        case FileClass::Cfg:
            return config->_oled->_menu->add_sd_file(path, false, true);
        case FileClass::None:
        default:
            return false;
    }
}

void sd_files_added(const char* full_path) {
    if (!config || !config->_oled) return;
    if (!sd_is_mounted) return;
    const char* relative = SDMenuEvents::strip_sd_prefix(full_path);
    if (relative == nullptr) return;        // not an SD path
    if (relative[0] == '\0') return;        // "/sd/" itself
    char ext[16];
    if (!SDMenuEvents::extract_lower_extension(relative, ext, sizeof(ext))) {
        return;
    }
    auto cls = SDMenuEvents::classify_extension(ext);
    if (cls == SDMenuEvents::FileClass::None) return;
    // Idempotency: defend against overwrite-uploads producing a
    // duplicate cache entry. add_sd_file itself does not check
    // whether the path is already present; it always appends.
    // remove_sd_file_entry returns false harmlessly if not found,
    // and walks all three SD menus so it works regardless of class.
    config->_oled->_menu->remove_sd_file_entry(relative);
    // add_sd_file takes char*, so make a mutable copy.
    char buf[LIST_NAME_MAX_PATH];
    strncpy(buf, relative, LIST_NAME_MAX_PATH);
    buf[LIST_NAME_MAX_PATH - 1] = '\0';
    if (!add_sd_file_for_class(cls, buf)) {
        // Hidden file (basename starts with `.`) or path-buffer
        // alloc failure. Both are recoverable for the menu cache --
        // hidden files are intentionally not shown, and an alloc
        // failure would surface in the populate path too.
        log_info("sd_files_added: skipped insert for " << full_path);
        return;
    }
    config->_oled->refresh_display(true);
}

void sd_files_removed(const char* full_path, bool was_directory) {
    if (!config || !config->_oled) return;
    if (!sd_is_mounted) return;
    const char* relative = SDMenuEvents::strip_sd_prefix(full_path);
    if (relative == nullptr) return;
    if (relative[0] == '\0') return;
    if (was_directory) {
        int n = config->_oled->_menu->remove_sd_subtree(relative);
        log_info("sd_files_removed (dir): " << full_path
                 << " removed " << n << " entries");
    } else {
        if (!config->_oled->_menu->remove_sd_file_entry(relative)) {
            log_warn("sd_files_removed: not in cache: " << full_path);
            return;
        }
    }
    config->_oled->refresh_display(true);
}

void sd_files_renamed(const char* old_full_path, const char* new_full_path) {
    if (!config || !config->_oled) return;
    if (!sd_is_mounted) return;
    if (old_full_path == nullptr || new_full_path == nullptr) return;
    // Directory-rename detection: rare path, fall back to full
    // rescan. is_directory may fail if FS state is unexpected
    // post-rename; treat error as "we don't know, rescan to be
    // safe."
    std::error_code ec;
    bool is_dir = std::filesystem::is_directory(new_full_path, ec);
    if (ec) {
        log_warn("sd_files_renamed: stat failed for " << new_full_path
                 << ": " << ec.message()
                 << " -- falling back to full rescan");
        sd_populate_files_menu();
        return;
    }
    if (is_dir) {
        sd_populate_files_menu();
        return;
    }
    const char* old_rel = SDMenuEvents::strip_sd_prefix(old_full_path);
    const char* new_rel = SDMenuEvents::strip_sd_prefix(new_full_path);
    if (old_rel == nullptr || new_rel == nullptr) return;
    config->_oled->_menu->remove_sd_file_entry(old_rel);
    config->_oled->_menu->remove_sd_file_entry(new_rel); // overwrite case
    // Classify the new path so we add to the right menu (rename
    // can change the class, e.g. .gcode -> .yaml). If the new name
    // is not menu-eligible, skip the add and accept the resulting
    // disappearance from the menu -- on-disk state is the source
    // of truth, and old_rel is already removed.
    char ext[16];
    SDMenuEvents::FileClass cls = SDMenuEvents::FileClass::None;
    if (SDMenuEvents::extract_lower_extension(new_rel, ext, sizeof(ext))) {
        cls = SDMenuEvents::classify_extension(ext);
    }
    if (cls == SDMenuEvents::FileClass::None) {
        log_info("sd_files_renamed: new name not menu-eligible: "
                 << new_full_path);
        config->_oled->refresh_display(true);
        return;
    }
    char buf[LIST_NAME_MAX_PATH];
    strncpy(buf, new_rel, LIST_NAME_MAX_PATH);
    buf[LIST_NAME_MAX_PATH - 1] = '\0';
    if (!add_sd_file_for_class(cls, buf)) {
        // Hidden basename or alloc failure; same handling as
        // sd_files_added.
        log_info("sd_files_renamed: skipped insert for "
                 << new_full_path);
    }
    config->_oled->refresh_display(true);
}

#endif
