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
    // Joins the SD mount point and an SD-relative path, tolerating a missing
    // leading '/' on the relative path.
    static std::string sd_path(const std::string& path) {
        std::string full = SD_CARD_MOUNT_POINT;
        if (path.empty() || path[0] != '/') {
            full += "/";
        }
        return full + path;
    }

    Failure update_firmware_from_sdcard(const std::string& path){

        std::string fw_path = sd_path(path);
        const char* fw_path_cstr = fw_path.c_str();

        log_info("Starting Firmware update from SD");
        log_info(fw_path_cstr);

        if(!sd_card_is_present()){
            log_warn("Firmware update: SD not inserted");
            return { "microSD card\nnot detected", false };
        }

        FILE* file = fopen(fw_path_cstr, "rb");
        if(file == NULL) {
            log_warn("Firmware update: cannot open " << fw_path);
            return { "Cannot open file", false };
        }

        fseek(file, 0, SEEK_END);
        size_t fw_size = ftell(file);
        fseek(file, 0, SEEK_SET);

        log_info("FW Size: " << float(fw_size/1000000.0f) << " MB");

        if(fw_size == 0){
            log_warn("Firmware update: file is empty");
            fclose(file);
            return { "File is empty", false };
        }

        if (Update.isRunning()) {
            // Another update (a web upload) owns the Updater. Don't abort it from here:
            // it runs in another task and abort() frees buffers it may be using.
            log_warn("Firmware update: another update is in progress");
            fclose(file);
            return { "Another update\nis in progress", false };
        }
        if (!Update.begin(fw_size)) { // Start with the size of the firmware
            log_warn("Firmware update: cannot begin: " << Update.errorString());
            fclose(file);
            return { Update.getError() == UPDATE_ERROR_SIZE ? "File too large" : "Cannot start\nupdate", false };
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
                log_warn("Firmware update: error reading file");
                Update.abort();
                fclose(file);
                return { "Error reading file", false };
            }

            size_t bytesWritten = Update.write(buffer, bytesRead);
            if (bytesWritten != bytesRead) {
                log_warn("Firmware update: error writing to flash");
                Update.abort();
                fclose(file);
                return { "Error writing flash", false };
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
            log_warn("Firmware update: error ending update: " << Update.getError());
            return { "Not a valid\nfirmware file", false };
        }

        log_info("Update successful, restarting...");
        esp_restart();
        return { "Restart failed", false };  // unreachable
    }


    Failure update_config_from_sdcard(const std::string& path){
        std::string cfg_in_path = sd_path(path);

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
            log_warn("Config update: local FS is neither spiffs nor littlefs, cannot write.");
            return { "No local filesystem", false };
        }
        const char* cfg_out_path_cstr = cfg_out_path.c_str();

        log_info("Starting Config update from SD");
        log_info(cfg_in_path_cstr);

        if(!sd_card_is_present()){
            log_warn("Config update: SD not inserted");
            return { "microSD card\nnot detected", false };
        }

        std::ifstream inputFile(cfg_in_path_cstr);
        if(!inputFile.is_open()) {
            log_warn("Config update: cannot open " << cfg_in_path);
            return { "Cannot open file", false };
        }
        // Reject an empty file before the output open truncates config.yaml: it would
        // otherwise copy "successfully" and restart onto an empty config.
        if (inputFile.peek() == std::ifstream::traits_type::eof()) {
            log_warn("Config update: " << cfg_in_path << " is empty");
            return { "File is empty", false };
        }

        std::ofstream outputFile(cfg_out_path_cstr);
        if(!outputFile.is_open()) {
            log_warn("Config update: cannot open " << cfg_out_path << " for writing");
            return { "Cannot write config", false };
        }

        log_info("Copying to local file...");
        log_info(cfg_out_path_cstr);

        std::string line;
        while(std::getline(inputFile, line) && outputFile) {
            outputFile << line << "\n";
        }

        // getline ends on EOF (eofbit) or on a read error (badbit); only EOF means the
        // whole file was read. Don't restart onto a copy known to be incomplete. The
        // local config.yaml has already been overwritten at this point, so say so.
        bool read_ok = inputFile.eof() && !inputFile.bad();
        inputFile.close();
        outputFile.close();
        bool write_ok = !outputFile.fail();
        if (!read_ok || !write_ok) {
            log_warn("Config update: copy incomplete (" << (write_ok ? "read" : "write")
                     << " error); " << cfg_out_path << " may be truncated");
            return { "Config may be\nincomplete", true };
        }

        log_info("Config update successful, restarting...");
        esp_restart();
        return { "Restart failed", false };  // unreachable
    }
}