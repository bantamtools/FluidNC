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

#ifdef USE_SDMMC

#define CHECK_EXECUTE_RESULT(err, str)                                                                                                     \
    do {                                                                                                                                   \
        if ((err) != ESP_OK) {                                                                                                             \
            log_error(str << " code 0x" << to_hex(err));                                                                                   \
            goto cleanup;                                                                                                                  \
        }                                                                                                                                  \
    } while (0)

static const String allowed_file_ext[SD_NUM_ALLOWED_EXT] = {".gcode", ".nc", ".txt"};
static bool sd_is_mounted = false;

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

    // Note: esp_vfs_fat_sdmmc_mount is all-in-one convenience functions.
    // Please check its source code and implement error recovery when developing
    // production applications.

    //bool host_inited = false;

    //sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    host_config.max_freq_khz = freq_hz / 1000;

    //err = host_config.init();
    //CHECK_EXECUTE_RESULT(err, "host init failed");
    //host_inited = true;

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

    //err = sdmmc_host_init_slot(host_config.slot, &slot_config);
    //CHECK_EXECUTE_RESULT(err, "slot init failed");

    // Empirically it is necessary to set the frequency twice.
    // If you do it only above, the max frequency will be pinned
    // at the highest "standard" frequency lower than the requested
    // one, which is 400 kHz for requested frequencies < 20 MHz.
    // If you do it only once below, the attempt to change it seems to
    // be ignored, and you get 20 MHz regardless of what you ask for.
    //if (freq_hz) {
    //    err = sdmmc_host_set_card_clk(host_config.slot, freq_hz / 1000);
    //    CHECK_EXECUTE_RESULT(err, "set slot clock speed failed");
    //}

    // Clear mount flag
    sd_is_mounted = false;

    return true;

///cleanup:
//    if (host_inited) {
 //       call_host_deinit(&host_config);
//    }
 //   return false;
}

// adapted from vfs_fat_sdmmc.c:esp_vfs_fat_sdmmc_mount()
std::error_code sd_mount(int max_files) {

    if (sd_is_mounted) return std::error_code(ESP_OK, std::system_category());

    log_info("Mount_sd");
    esp_err_t err;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = max_files,
        .allocation_unit_size = 64 * 1024
    };

    err = esp_vfs_fat_sdmmc_mount(base_path, &host_config, &slot_config, &mount_config, &card);

    CHECK_EXECUTE_RESULT(err, "sd_mount failed");

    sd_is_mounted = true;


/*
    // mount_prepare_mem() ... minus the strdup of base_path
    // Search for a free drive slot
    BYTE pdrv = FF_DRV_NOT_USED;
    if ((err = ff_diskio_get_drive(&pdrv)) != ESP_OK) {
        log_debug("ff_diskio_get_drive failed");
        return std::error_code(err, std::system_category());
    }
    if (pdrv == FF_DRV_NOT_USED) {
        log_debug("the maximum count of volumes is already mounted");
        return std::error_code(ESP_FAIL, std::system_category());
    }
    // pdrv is now the index of the unused drive slot

    // not using ff_memalloc here, as allocation in internal RAM is preferred
    card = (sdmmc_card_t*)malloc(sizeof(sdmmc_card_t));
    if (card == NULL) {
        log_debug("could not allocate new sdmmc_card_t");
        return std::error_code(ESP_ERR_NO_MEM, std::system_category());
    }
    // /mount_prepare_mem()

    // probe and initialize card
    err = sdmmc_card_init(&host_config, card);
    CHECK_EXECUTE_RESULT(err, "sdmmc_card_init failed");

    err = mount_to_vfs_fat(max_files, card, pdrv, base_path);
    CHECK_EXECUTE_RESULT(err, "mount_to_vfs failed");

    // set flag
    sd_is_mounted = true;
*/


    return {};
cleanup:
 //   free(card);
 //   card = NULL;
    return std::error_code(err, std::system_category());
}

void sd_unmount() {

    esp_err_t err;

    log_info("Unmount_sd");

    // Unmount SD card if previously mounted
    if (sd_is_mounted) {

        err = esp_vfs_fat_sdcard_unmount(base_path, card);
        CHECK_EXECUTE_RESULT(err, "sd_unmount failed");
        
        sd_is_mounted = false;
    }
cleanup:
    return;

    /*
    BYTE pdrv = ff_diskio_get_pdrv_card(card);
    if (pdrv == 0xff) {
        return;
    }

    // unmount
    char drv[3] = { (char)('0' + pdrv), ':', 0 };
    f_mount(NULL, drv, 0);

    esp_vfs_fat_unregister_path(base_path);

    // release SD driver
    ff_diskio_unregister(pdrv);

    free(card);
    card = NULL;

    // clear flag
    sd_is_mounted = false;
    */
}

/*
void sd_deinit_slot() {
    // log_debug("Deinit slot");
    call_host_deinit(&host_config);
}*/

bool sd_card_is_present() {

    return sd_is_mounted;
}

void sd_populate_files_menu() {

    std::error_code ec;
    const std::filesystem::path fpath{base_path};
    char file_ext[LIST_NAME_MAX_PATH];
    char file_path[LIST_NAME_MAX_PATH];

    // No display, bail
    if (!config->_oled) {
        return;
    }

    // Clear the file list to start
    config->_oled->_menu->prep_for_sd_update();

    // SD not mounted, attempt to mount
    //if (!sd_is_mounted) {
    //    ec = sd_mount();
    //}

    // Iterate through files if no errors (i.e. SD not found or corrupt)
    if (sd_is_mounted) {

        // Iterate through the top level directory
        auto iter = std::filesystem::recursive_directory_iterator { fpath, ec };
        if (!ec) {

            for (auto const& dir_entry : iter) {

                // Get the file extension and convert to lowercase
                strncpy(file_ext, dir_entry.path().extension().c_str(), LIST_NAME_MAX_PATH);
                for (auto i = 0; file_ext[i]; i++) {
                    file_ext[i] = tolower(file_ext[i]);
                }

                // Iterate through allowed file extensions
                for (auto i = 0; i < SD_NUM_ALLOWED_EXT; i++) {

                    // Found a matching extension, list file
                    if (strcmp(file_ext, allowed_file_ext[i].c_str()) == 0) {

                        // Save file name to menu
                        std::string full_path = dir_entry.path().c_str();
                        std::string short_path = full_path.substr(strlen(base_path));

                        strncpy(file_path, short_path.c_str(), LIST_NAME_MAX_PATH);
                        config->_oled->_menu->add_sd_file(file_path);
                    }    
                }
            }
        }
        
        // Unmount the SD card
        //sd_unmount();
    }

    // Refresh the menu
    config->_oled->refresh_display(true);
}
#endif
