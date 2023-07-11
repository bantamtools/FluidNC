#include "vfs_api.h"
#include "esp_vfs_fat.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "ff.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"

#include "Driver/sdspi.h"
#include "src/Config.h"

#define CHECK_EXECUTE_RESULT(err, str)                                                                                                     \
    do {                                                                                                                                   \
        if ((err) != ESP_OK) {                                                                                                             \
            log_error(str << " code 0x" << to_hex(err));                                                                                   \
            goto cleanup;                                                                                                                  \
        }                                                                                                                                  \
    } while (0)

static FileListType sd_files;
static const String allowed_file_ext[SD_NUM_ALLOWED_EXT] = {".gcode", ".nc", ".txt"};
static bool sd_is_mounted = false;

// SD card detect handler
static void IRAM_ATTR card_detect_handler(void *args)
{
    // Print message to channel to trigger updates
    log_info("SD Card Detect Event");
}

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

sdmmc_host_t  host_config = SDSPI_HOST_DEFAULT();
sdmmc_card_t* card        = NULL;
const char*   base_path   = "/sd";

static void call_host_deinit(const sdmmc_host_t* host_config) {
    if (host_config->flags & SDMMC_HOST_FLAG_DEINIT_ARG) {
        host_config->deinit_p(host_config->slot);
    } else {
        host_config->deinit();
    }
}

bool sd_init_slot(uint32_t freq_hz, int cs_pin, int cd_pin, int wp_pin) {
    esp_err_t err;

    // Note: esp_vfs_fat_sdmmc/sdspi_mount is all-in-one convenience functions.
    // Please check its source code and implement error recovery when developing
    // production applications.

    bool host_inited = false;

    sdspi_device_config_t slot_config;

    host_config.max_freq_khz = freq_hz / 1000;

    err = host_config.init();
    CHECK_EXECUTE_RESULT(err, "host init failed");
    host_inited = true;

    // Attach a set of GPIOs to the SPI SD card slot
    slot_config         = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = static_cast<spi_host_device_t>(host_config.slot);
    slot_config.gpio_cs = gpio_num_t(cs_pin);
    slot_config.gpio_cd = gpio_num_t(cd_pin);
    slot_config.gpio_wp = gpio_num_t(wp_pin);

    err = sdspi_host_init_device(&slot_config, &(host_config.slot));
    CHECK_EXECUTE_RESULT(err, "slot init failed");

    // Empirically it is necessary to set the frequency twice.
    // If you do it only above, the max frequency will be pinned
    // at the highest "standard" frequency lower than the requested
    // one, which is 400 kHz for requested frequencies < 20 MHz.
    // If you do it only once below, the attempt to change it seems to
    // be ignored, and you get 20 MHz regardless of what you ask for.
    if (freq_hz) {
        err = sdspi_host_set_card_clk(host_config.slot, freq_hz / 1000);
        CHECK_EXECUTE_RESULT(err, "set slot clock speed failed");
    }

    // Attach card detect interrupt
    gpio_mode(cd_pin, true, false, false, false, false);
    //gpio_add_interrupt(cd_pin, Pin::EITHER_EDGE, card_detect_handler, (void *)gpio_num_t(cd_pin));

    // Clear file count on file list and mount flag
    sd_files.num_files = 0;
    sd_is_mounted = false;

    return true;

cleanup:
    if (host_inited) {
        call_host_deinit(&host_config);
    }
    return false;
}

#if 0
bool init_spi_bus(int mosi_pin, int miso_pin, int clk_pin) {
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = mosi_pin,
        .miso_io_num     = miso_pin,
        .sclk_io_num     = clk_pin,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4000,
    };
    esp_err_t err = spi_bus_initialize(host_config.slot, &bus_cfg, SPI_DMA_CHAN);
    return err == ESP_OK;
}
#endif

// adapted from vfs_fat_sdmmc.c:esp_vfs_fat_sdmmc_mount()
std::error_code sd_mount(int max_files) {
    log_info("Mount_sd");
    esp_err_t err;

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

    return {};
cleanup:
    free(card);
    card = NULL;
    return std::error_code(err, std::system_category());
}

void sd_unmount() {
    log_info("Unmount_sd");
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
}

void sd_deinit_slot() {
    // log_debug("Deinit slot");
    sdspi_host_remove_device(host_config.slot);
    call_host_deinit(&host_config);

    //deinitialize the bus after all devices are removed
    //    spi_bus_free(HSPI_HOST);
}

#if 0
static esp_err_t unmount_card_core(const char* base_path, sdmmc_card_t* card) {
    return err;
}

unmount2() {
    char drv[3] = { (char)('0' + pdrv), ':', 0 };
    f_mount(NULL, drv, 0);
}
#endif

void sd_list_files() {

    std::error_code ec;
    const std::filesystem::path fpath{base_path};
    char file_ext[SD_MAX_STR];

    // Clear the file list to start
    sd_files.num_files = 0;

    // SD not mounted, attempt to mount
    if (!sd_is_mounted) {
        ec = sd_mount();
    }

    // Iterate through files if no errors (i.e. SD not found or corrupt)
    if (!ec) {

        // Iterate through the top level directory
        auto iter = std::filesystem::recursive_directory_iterator { fpath, ec };
        if (!ec) {

            for (auto const& dir_entry : iter) {

                // Get the file extension and convert to lowercase
                strncpy(file_ext, dir_entry.path().extension().c_str(), SD_MAX_STR);
                for (auto i = 0; file_ext[i]; i++) {
                    file_ext[i] = tolower(file_ext[i]);
                }

                // Iterate through allowed file extensions
                for (auto i = 0; i < SD_NUM_ALLOWED_EXT; i++) {

                    // Found a matching extension, list file
                    if (strcmp(file_ext, allowed_file_ext[i].c_str()) == 0) {

                        // Reached maximum file list, display error and exit listing loop
                        if (sd_files.num_files == SD_MAX_FILES) {
                            log_warn("Maximum file list reached, stopped listing files");
                            break;
                        }

                        // Save file name to list and increment count
                        std::string full_path = dir_entry.path().c_str();
                        std::string short_path = full_path.substr(strlen(base_path));

                        strncpy(sd_files.path[sd_files.num_files], short_path.c_str(), SD_MAX_STR);
                        sd_files.num_files++;
                    }    
                }
            }
        }

        // Unmount the SD card
        sd_unmount();
    }
}

FileListType *sd_get_filelist(void) {

    sd_list_files();
    return &sd_files;
}
