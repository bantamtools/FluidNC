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
#include "src/SDFiles/SDScan.h"
#include "src/SDFiles/SDFileTable.h"
#include "src/SDFiles/MenuSortConfig.h"
#include "src/SDFiles/FsTime.h"

#include <unordered_set>
#include <filesystem>
#include <system_error>
#include <cstring>
#include <cstdio>
#include <chrono>
#include <cstdint>
#include <mutex>

#ifdef USE_SDMMC

// static const String allowed_file_ext[SD_NUM_ALLOWED_EXT] = {".gcode", ".nc", ".txt"};
// static const String allowed_binary_ext[SD_NUM_BIN_EXT] = {".bin"};

static const std::unordered_set<std::string>allowed_file_ext({".gcode", ".nc", ".txt"});
static const std::unordered_set<std::string>allowed_binary_ext({".bin"});
static const std::unordered_set<std::string>allowed_config_ext({".yaml"});

static bool sd_is_mounted = false;
static uint32_t _freq_hz = 20000000;

// Tracks physical card presence as reported by the card-detect pin.
// True means a card is physically seated; false means absent.
// Updated by sd_set_card_present() when a card-detect pin is configured.
// Remains true (permissive) on boards with no card-detect pin.
static bool sd_cd_pin_present = true;

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

    // When a card-detect pin is configured and reports no card, skip the
    // blocking mount call — esp_vfs_fat_sdmmc_mount() hangs indefinitely
    // when no card is present. Boards with no card-detect pin leave
    // sd_cd_pin_present true, so this guard never fires for them.
    if (!sd_cd_pin_present) {
        log_info("sd_mount: no card detected, skipping mount");
        return std::error_code(ESP_ERR_NOT_FOUND, std::system_category());
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

// Called by CardDetectPin::update() to record physical card presence.
// Only boards with a card-detect pin call this; on boards without one,
// sd_cd_pin_present stays true and sd_mount() always attempts the mount.
void sd_set_card_present(bool present) {
    sd_cd_pin_present = present;
}

// Returns the last-write-time of full_path as a mtime sort key, or 0 on error.
static uint32_t mtime_for_path(const char* full_path) {
    std::error_code ec;
    auto t = std::filesystem::last_write_time(full_path, ec);
    return ec ? 0 : sdfiles::toMtimeSeconds(t);
}

// Opens the SD browser for `cls` on menu `m`. If the resulting list is empty
// (only the Back row), shows the appropriate "no files" popup instead of
// activating the browser. Mirrors Menu::enter_submenu()'s empty-list handling
// so both code paths stay consistent.
static void sd_reopen_browser(Menu* m, sdfiles::FileClass cls) {
    m->sd_browser().open(cls);
    size_t row_count;
    {
        std::lock_guard<std::recursive_mutex> lk(m->sd_table().mutex());
        row_count = m->sd_browser().rowCount(m->sd_table());
    }
    if (row_count == 1) {
        // Only the Back row — nothing to browse.
        const char* empty_msg;
        if (!sd_card_is_present()) {
            empty_msg = "No microSD Card";
        } else if (cls == sdfiles::FileClass::Firmware) {
            empty_msg = "No firmware files\non microSD Card";
        } else if (cls == sdfiles::FileClass::Config) {
            empty_msg = "No config files\non microSD Card";
        } else {
            empty_msg = "No G-code files\non microSD Card";
        }
        log_info("No files of the selected type on SD");
        config->_oled->popup_msg(empty_msg, 0, true, OLED::PopupLevel::Status);
        // Leave _sd_browse_active false — don't enter an empty list view.
    } else {
        m->set_sd_browse_active(true);
    }
}

void sd_populate_files_menu() {
    std::error_code ec;
    const std::filesystem::path fpath{base_path};
    char file_ext[LIST_NAME_MAX_PATH];
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

    // One lock for the whole rebuild: the arena reset + disk scan + rebuildIndex must be
    // atomic against the other core's readers. Recursive mutex -- the progress popups and
    // the trailing refresh_display() below re-enter via show_menu(). This holds the lock
    // across the (seconds-long) scan, briefly freezing the scroll-render; accepted for the
    // rare card-insert / boot / rename-fallback paths.
    std::lock_guard<std::recursive_mutex> lk(config->_oled->_menu->sd_table().mutex());

    // Clear the file list to start
    config->_oled->_menu->prep_for_sd_update();

    // Only scan if card is actually mounted
    if (!sd_is_mounted) {
        log_info("SD card not mounted, skipping file scan");
        // Capture whether the user was actively browsing an SD list before
        // clearing that state, so removal-while-browsing can show a popup.
        bool was_browsing = config->_oled->_menu->sd_browse_active();
        // Clear the arena and reset browser state so the OLED shows an
        // empty, safe list rather than stale entries from before removal.
        config->_oled->_menu->sd_table().reset();
        config->_oled->_menu->sd_browser().resetToRoot();
        config->_oled->_menu->set_sd_browse_active(false);
        config->_oled->_menu->finish_sd_update();
        // Inform the user when a card is removed while they are browsing.
        if (was_browsing) {
            config->_oled->popup_msg("No microSD Card", 0, true, OLED::PopupLevel::Status);
        }
        config->_oled->refresh_display(true);
        return;
    }

    // Iterate through files if no errors (i.e. SD not found or corrupt)
    if (sd_is_mounted) {
        log_info("SD is mounted");
        try {
            // The arena is the sole source of SD content. Reset here; rebuildIndex
            // once after the loop.
            config->_oled->_menu->sd_table().reset();

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

                    if (dir_entry.is_directory()) {
                        std::string full_path  = dir_entry.path().string();
                        std::string short_path = full_path.substr(strlen(base_path));
                        sdfiles::SDScan::addScannedEntry(
                            config->_oled->_menu->sd_table(),
                            short_path.c_str(), /*isDir=*/true, /*mtime=*/0,
                            sdfiles::FileClass::Gcode);  // class don't-care for dirs
                    }

                    // Get the file extension and convert to lowercase
                    std::string extension = dir_entry.path().extension().string();
                    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

                    bool added = false;

                    // Check if the file extension is in the allowed file extensions
                    if (allowed_file_ext.count(extension) > 0) {
                        std::string full_path = dir_entry.path().string();
                        std::string short_path = full_path.substr(strlen(base_path));

                        uint32_t mtime = 0;
                        try { mtime = sdfiles::toMtimeSeconds(dir_entry.last_write_time()); } catch (...) {}

                        // The arena accept/reject decides whether this file counts
                        // toward the menu, recent-file tracking, and progress popups.
                        added = sdfiles::SDScan::addScannedEntry(
                            config->_oled->_menu->sd_table(),
                            short_path.c_str(), /*isDir=*/false, mtime,
                            sdfiles::FileClass::Gcode);
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

                        // Log free heap every 20 files for diagnostics. File entries live
                        // in the fixed arena, so loading does not consume heap per file
                        // (free heap stays flat across a scan) -- the arena full() check
                        // below is the real, heap-independent bound. No heap watchdog: a
                        // free-heap threshold here only reflected the baseline (WiFi + the
                        // arena), not the scan, so it spuriously truncated the list when
                        // WiFi left less than the threshold free.
                        if (file_count % 20 == 0) {
                            log_info("Files read: " << file_count
                                     << ", Heap: " << ESP.getFreeHeap() << " bytes");

                            // Show loading progress (starting at 40 files)
                            if (file_count > 39) {
                                char msg[55];
                                snprintf(msg, sizeof(msg), "microSD Card:\nReading %d files...", file_count);
                                config->_oled->popup_msg(msg, 0, true, OLED::PopupLevel::Status);
                                config->_oled->processDisplayRefresh(); // Force display update
                            }
                        }
                    }

                    // Check if the file extension is in the allowed binary extensions
                    else if (allowed_binary_ext.count(extension) > 0) {
                        std::string full_path = dir_entry.path().string();
                        std::string short_path = full_path.substr(strlen(base_path));

                        uint32_t mtime = 0;
                        try { mtime = sdfiles::toMtimeSeconds(dir_entry.last_write_time()); } catch (...) {}
                        if (sdfiles::SDScan::addScannedEntry(
                                config->_oled->_menu->sd_table(),
                                short_path.c_str(), /*isDir=*/false, mtime,
                                sdfiles::FileClass::Firmware)) {
                            file_count++;
                        }
                        //SAVE CONFIG PATH TO CONFIG
                    }

                    // Check if the file extension is in the allowed config extensions
                    else if (allowed_config_ext.count(extension) > 0) {
                        std::string full_path = dir_entry.path().string();
                        std::string short_path = full_path.substr(strlen(base_path));

                        uint32_t mtime = 0;
                        try { mtime = sdfiles::toMtimeSeconds(dir_entry.last_write_time()); } catch (...) {}
                        if (sdfiles::SDScan::addScannedEntry(
                                config->_oled->_menu->sd_table(),
                                short_path.c_str(), /*isDir=*/false, mtime,
                                sdfiles::FileClass::Config)) {
                            file_count++;
                        }
                    }

                    // Stop once the arena can hold no more entries. Further adds are
                    // rejected, so file_count freezes and the other stop conditions never
                    // fire; breaking here avoids grinding the rest of the card.
                    if (config->_oled->_menu->sd_table().full()) {
                        log_warn("Arena capacity reached: " << file_count << " files; stopping scan");
                        config->_oled->clear_popup(OLED::PopupLevel::Status);
                        char msg[55];
                        snprintf(msg, sizeof(msg), "File limit reached:\nRead %d files from microSD.", file_count);
                        config->_oled->popup_msg(msg, 0, true, OLED::PopupLevel::Status);
                        limit_reached = true;
                        break;
                    }

                    // Check if we've reached the absolute file limit
                    if (file_count >= MAX_SD_FILES) {
                        log_warn("Absolute file limit reached: " << file_count << " files read, stopping scan");
                        config->_oled->clear_popup(OLED::PopupLevel::Status);  // Clear loading message first
                        char msg[55];
                        snprintf(msg, sizeof(msg), "File limit reached:\nRead %d files from microSD.", file_count);
                        config->_oled->popup_msg(msg, 0, true, OLED::PopupLevel::Status);
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

                // Sort the freshly-populated arena using the configured sort mode.
                config->_oled->_menu->sd_table().rebuildIndex(sdfiles::menuSortMode());

                // A full rescan reassigned EntryIds, so the browser's currentDir must
                // reset to root (its class filter is preserved).
                config->_oled->_menu->sd_browser().resetToRoot();

                // Clear any stale popup (e.g. "No microSD Card" from a prior
                // removal) so the freshly-populated list is shown immediately.
                // Re-open the browser for whichever SD class menu the user is
                // sitting on, so inserting a card while the list is empty (or
                // while a no-card popup is up) shows the populated list rather
                // than leaving the view empty or stuck on the popup.
                config->_oled->clear_popup(OLED::PopupLevel::Status);
                {
                    auto* m = config->_oled->_menu;
                    if (m->is_files_menu()) {
                        sd_reopen_browser(m, sdfiles::FileClass::Gcode);
                    } else if (m->is_firmware_menu()) {
                        sd_reopen_browser(m, sdfiles::FileClass::Firmware);
                    } else if (m->is_config_menu()) {
                        sd_reopen_browser(m, sdfiles::FileClass::Config);
                    }
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
        config->_oled->clear_popup(OLED::PopupLevel::Status);
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

// Map the event classifier's FileClass to the arena's FileClass. Called only with a
// menu-eligible class (the event functions return early on FileClass::None).
static sdfiles::FileClass to_arena_class(SDMenuEvents::FileClass cls) {
    switch (cls) {
        case SDMenuEvents::FileClass::Bin: return sdfiles::FileClass::Firmware;
        case SDMenuEvents::FileClass::Cfg: return sdfiles::FileClass::Config;
        case SDMenuEvents::FileClass::Gcode:
        default:                           return sdfiles::FileClass::Gcode;
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
    // Upsert into the arena. addOrReplaceEntry is idempotent (overwrite-uploads
    // do not produce duplicates) and rejects hidden dotfiles internally. mtime is
    // read from the filesystem so date-based sort orders reflect the actual file
    // timestamp. A rejection (hidden file or full arena) skips the resort/refresh below.
    {
        std::lock_guard<std::recursive_mutex> lk(config->_oled->_menu->sd_table().mutex());
        if (!sdfiles::SDScan::addOrReplaceEntry(config->_oled->_menu->sd_table(), relative,
                                                /*isDir=*/false, mtime_for_path(full_path),
                                                to_arena_class(cls))) {
            log_info("sd_files_added: skipped insert for " << full_path);
            return;
        }
        config->_oled->_menu->sd_table().rebuildIndex(sdfiles::menuSortMode());
    }
    config->_oled->refresh_display(true);
}

void sd_files_removed(const char* full_path, bool was_directory) {
    if (!config || !config->_oled) return;
    if (!sd_is_mounted) return;
    const char* relative = SDMenuEvents::strip_sd_prefix(full_path);
    if (relative == nullptr) return;
    if (relative[0] == '\0') return;
    {
        std::lock_guard<std::recursive_mutex> lk(config->_oled->_menu->sd_table().mutex());
        if (was_directory) {
            sdfiles::SDScan::removeSubtreeByPath(config->_oled->_menu->sd_table(), relative);
            log_info("sd_files_removed (dir): " << full_path);
        } else {
            if (!sdfiles::SDScan::removeEntryByPath(config->_oled->_menu->sd_table(), relative)) {
                log_warn("sd_files_removed: not in cache: " << full_path);
                return;
            }
        }
        // rebuildIndex runs only on a successful file removal; a directory removal always
        // proceeds to resort the view.
        config->_oled->_menu->sd_table().rebuildIndex(sdfiles::menuSortMode());
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
    // Classify the new path (pure, no arena access) so we add to the right menu --
    // a rename can change the class (.gcode -> .yaml). A non-eligible new name skips
    // the add: on-disk state is the source of truth, and old_rel is removed regardless.
    char ext[16];
    SDMenuEvents::FileClass cls = SDMenuEvents::FileClass::None;
    if (SDMenuEvents::extract_lower_extension(new_rel, ext, sizeof(ext))) {
        cls = SDMenuEvents::classify_extension(ext);
    }
    {
        std::lock_guard<std::recursive_mutex> lk(config->_oled->_menu->sd_table().mutex());
        // Remove both the old path and the new path (overwrite case) from the arena.
        sdfiles::SDScan::removeEntryByPath(config->_oled->_menu->sd_table(), old_rel);
        sdfiles::SDScan::removeEntryByPath(config->_oled->_menu->sd_table(), new_rel);
        if (cls == SDMenuEvents::FileClass::None) {
            log_info("sd_files_renamed: new name not menu-eligible: " << new_full_path);
        } else {
            // Add the new path. addOrReplaceEntry rejects hidden dotfiles internally;
            // on rejection old_rel is already removed (drops a rename-to-hidden). mtime
            // comes from the new path; FatFS does not update the timestamp on rename.
            bool added = sdfiles::SDScan::addOrReplaceEntry(config->_oled->_menu->sd_table(), new_rel,
                                                            /*isDir=*/false, mtime_for_path(new_full_path),
                                                            to_arena_class(cls));
            if (!added) {
                log_info("sd_files_renamed: arena rejected new name (hidden or full): " << new_full_path);
            }
        }
        config->_oled->_menu->sd_table().rebuildIndex(sdfiles::menuSortMode());
    }
    config->_oled->refresh_display(true);
}

#endif
