#include "Flashing.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
// #include "esp_ota_ops.h"
#include "../include/Driver/sdmmc.h"
#include "../include/Driver/localfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>
#include <fstream>

#include <Update.h>

#define SD_CARD_MOUNT_POINT "/sd" // Base path from sdmmc.

namespace Flashing {
    void update_firmware_from_sdcard(std::string& filename){

        std::string fw_path = std::string(SD_CARD_MOUNT_POINT) + "/" + filename;
        const char* fw_path_cstr = fw_path.c_str();

        log_info("Starting Firmware update from SD");
        log_info(fw_path_cstr);

        if(!sd_card_is_present()){
            log_info("SD not inserted");
            return;
        }

        FILE* file = fopen(fw_path_cstr, "rb");
        if(file == NULL) {
            log_error("Missing \"firmware.bin\"");
            return;
        }

        fseek(file, 0, SEEK_END);
        size_t fw_size = ftell(file);
        fseek(file, 0, SEEK_SET);

        log_info("FW Size: " << float(fw_size/1000000.0f) << " MB");

        if(fw_size == 0){
            log_error("Firmware file is empty");
            fclose(file);
            return;
        }

        if (!Update.begin(fw_size)) { // Start with the size of the firmware
            log_error("Not enough space to begin OTA");
            fclose(file);
            return;
        }

        size_t written = 0;
        size_t total = fw_size;
        uint8_t buffer[1024];
        int prev_progress = -1;

        while (written < total) {
            size_t toRead = sizeof(buffer);
            if ((total - written) < toRead) {
                toRead = total - written;
            }

            size_t bytesRead = fread(buffer, 1, toRead, file);
            if (bytesRead != toRead) {
                log_error("Error reading firmware file");
                Update.abort();
                fclose(file);
                return;
            }

            size_t bytesWritten = Update.write(buffer, bytesRead);
            if (bytesWritten != bytesRead) {
                log_error("Error writing firmware to flash");
                Update.abort();
                fclose(file);
                return;
            }

            written += bytesWritten;

            // Optionally log progress
            int progress = (int)((written * 100) / total);
            if(progress != prev_progress){
                log_info("Update progress: " << progress << "%");
                prev_progress = progress;
            }
        }

        fclose(file);

        if (!Update.end(true)) { // true to set the size to the current progress
            log_error("Error ending update: " << Update.getError());
            return;
        } else {
            log_info("Update successful, restarting...");
            esp_restart();
        }

        return;
    }


    void update_config_from_sdcard(std::string& filename, bool addMount){
        std::string cfg_in_path;

        if(addMount){
            cfg_in_path = std::string(SD_CARD_MOUNT_POINT) + "/" + filename;
        } else {
            cfg_in_path = filename;
        }
        
        const char* cfg_in_path_cstr = cfg_in_path.c_str();
        //std::string cfg_out_path = "/localfs/config_test.yaml"; // temp test, doesn't work
        //std::string cfg_out_path = "/spiffs/config_test.yaml"; // temp test, works!
        // Update Mar '25: Some boards end up with spiffs, others with littlefs, so...
        std::string cfg_out_path;
        if (localfsName == spiffsName) {
            cfg_out_path = "/spiffs/config.yaml"; // overwrite config.yaml
            log_info("Using spiffs for local fs write...");
        } else if (localfsName == littlefsName) {
            cfg_out_path = "/littlefs/config.yaml";
            log_info("Using littlefs for local fs write...");
        } else {
            log_error("Local FS is neither spiffs nor littlefs, cannot write.");
            return;
        }
        const char* cfg_out_path_cstr = cfg_out_path.c_str();

        log_info("Starting Config update from SD");
        log_info(cfg_in_path_cstr);

        if(!sd_card_is_present()){
            log_info("SD not inserted");
            return;
        }

        std::ifstream inputFile(cfg_in_path_cstr);
        if(!inputFile.is_open()) {
            log_error("Could not open input config file from SD");
            return;
        }

        std::ofstream outputFile(cfg_out_path_cstr);
        if(!outputFile.is_open()) {
            log_error("Could not open output config file on local FS for writing");
            return;
        }

        log_info("Copying to local file...");
        log_info(cfg_out_path_cstr);

        std::string line;
        while(std::getline(inputFile, line)) {
            outputFile << line << "\n";
        }

        inputFile.close();
        outputFile.close();

        log_info("Config update successful, restarting...");
        esp_restart();

        return;
    }
}