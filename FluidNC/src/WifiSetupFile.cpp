#include "WifiSetupFile.h"
#include "Machine/MachineConfig.h"
#include "WebUI/WifiConfig.h"
#include "OLED.h"
#include "SSD1306_I2C.h"
#include "Driver/sdmmc.h"
#include "NutsBolts.h"
#include <cstdio>
#include <cstring>
#include <Esp.h>

#ifdef ENABLE_WIFI

static const char* WIFI_SETUP_PATH = "/sd/wifi_setup.txt";

// Show a persistent OLED message and force display update during boot
static void wifi_setup_oled_msg(const char* msg) {
    if (config && config->_oled) {
        config->_oled->show_persistent_msg(std::string(msg));
        SSD1306_I2C* ssd1306 = static_cast<SSD1306_I2C*>(config->_oled->_oled);
        if (ssd1306) {
            ssd1306->performDisplayUpdate();
        }
    }
}

// Wait for enter button press (blocking, for use during boot)
static void wait_for_button_press() {
    if (!config || !config->_control) return;
    // Wait for button release first (in case it's held from boot)
    while (config->_control->enter_pressed()) {
        delay_ms(50);
    }
    // Wait for press
    while (!config->_control->enter_pressed()) {
        delay_ms(50);
    }
    // Wait for release
    while (config->_control->enter_pressed()) {
        delay_ms(50);
    }
}

// Reboot with OLED feedback
static void reboot_on_click() {
    wait_for_button_press();
    wifi_setup_oled_msg("Now Rebooting...");
    ESP.restart();
    while (1) {}
}

// Trim leading/trailing whitespace in place, return pointer to trimmed start
static char* ws_trim(char* s) {
    while (*s == ' ' || *s == '\t') s++;
    char* end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0';
        end--;
    }
    return s;
}

void check_wifi_setup_file() {
    // SD card must be mounted
    if (!sd_card_is_present()) return;

    // Check if file exists
    FILE* file = fopen(WIFI_SETUP_PATH, "r");
    if (!file) return;

    log_info("wifi_setup.txt detected on SD card");

    // Parse the file
    char line[96];
    char mode_val[16] = {0};
    char ssid_val[33] = {0};   // max SSID is 32 chars
    char pass_val[64] = {0};   // max WiFi password is 63 chars
    char hostname_val[33] = {0};
    bool has_mode = false;
    bool has_ssid = false;
    bool has_password = false;
    bool has_hostname = false;
    bool parse_error = false;
    char error_msg[80] = {0};

    while (fgets(line, sizeof(line), file)) {
        char* trimmed = ws_trim(line);
        if (trimmed[0] == '\0') continue;  // skip blank lines

        // Find the colon separator
        char* colon = strchr(trimmed, ':');
        if (!colon) {
            snprintf(error_msg, sizeof(error_msg), "wifi_setup.txt: bad line format");
            parse_error = true;
            break;
        }

        // Split key and value
        *colon = '\0';
        char* key = ws_trim(trimmed);
        char* value = ws_trim(colon + 1);

        if (strcmp(key, "mode") == 0) {
            strncpy(mode_val, value, sizeof(mode_val) - 1);
            has_mode = true;
        } else if (strcmp(key, "ssid") == 0) {
            strncpy(ssid_val, value, sizeof(ssid_val) - 1);
            has_ssid = true;
        } else if (strcmp(key, "password") == 0) {
            strncpy(pass_val, value, sizeof(pass_val) - 1);
            has_password = true;
        } else if (strcmp(key, "hostname") == 0) {
            strncpy(hostname_val, value, sizeof(hostname_val) - 1);
            has_hostname = true;
        } else {
            snprintf(error_msg, sizeof(error_msg), "wifi_setup.txt: unknown key '%s'", key);
            parse_error = true;
            break;
        }
    }
    fclose(file);

    // Validate
    if (parse_error) {
        log_error(error_msg);
        wifi_setup_oled_msg(error_msg);
        return;  // file preserved
    }

    if (!has_mode) {
        log_error("wifi_setup.txt: missing 'mode'");
        wifi_setup_oled_msg("wifi_setup.txt: missing 'mode'");
        return;
    }

    // Determine mode
    bool is_join = (strcmp(mode_val, "join") == 0);
    bool is_hotspot = (strcmp(mode_val, "hotspot") == 0);
    bool is_off = (strcmp(mode_val, "off") == 0);

    if (!is_join && !is_hotspot && !is_off) {
        char msg[80];
        snprintf(msg, sizeof(msg), "wifi_setup.txt: invalid mode '%s'", mode_val);
        log_error(msg);
        wifi_setup_oled_msg(msg);
        return;
    }

    // For join/hotspot, ssid and password lines are required
    if ((is_join || is_hotspot) && !has_ssid) {
        log_error("wifi_setup.txt: missing 'ssid'");
        wifi_setup_oled_msg("wifi_setup.txt: missing 'ssid'");
        return;
    }
    if ((is_join || is_hotspot) && !has_password) {
        log_error("wifi_setup.txt: missing 'password'");
        wifi_setup_oled_msg("wifi_setup.txt: missing 'password'");
        return;
    }
    if ((is_join || is_hotspot) && strlen(ssid_val) == 0) {
        log_error("wifi_setup.txt: ssid cannot be empty");
        wifi_setup_oled_msg("wifi_setup.txt: ssid is empty");
        return;
    }
    if (has_hostname && strlen(hostname_val) == 0) {
        log_error("wifi_setup.txt: hostname cannot be empty");
        wifi_setup_oled_msg("wifi_setup.txt: hostname is empty");
        return;
    }

    // --- File parsed successfully ---

    bool config_controls_mode = (config->_wifiMode >= 0);

    // Write credentials to NVS
    if (is_join) {
        WebUI::wifi_sta_ssid->setStringValue(ssid_val);
        WebUI::wifi_sta_password->setStringValue(pass_val);
        log_info("WiFi STA credentials updated from wifi_setup.txt");
        if (!config_controls_mode) {
            WebUI::wifi_mode->setStringValue((char*)"STA>AP");
            log_info("WiFi mode set to Fallback");
        }
    } else if (is_hotspot) {
        WebUI::wifi_ap_ssid->setStringValue(ssid_val);
        WebUI::wifi_ap_password->setStringValue(pass_val);
        log_info("WiFi AP credentials updated from wifi_setup.txt");
        if (!config_controls_mode) {
            WebUI::wifi_mode->setStringValue((char*)"AP");
            log_info("WiFi mode set to AP");
        }
    } else if (is_off) {
        if (!config_controls_mode) {
            // Save current mode for restore-on-toggle before turning off
            int8_t currentMode = WebUI::wifi_mode->get();
            if (currentMode != 0 && currentMode != WebUI::wifi_on_mode->get()) {
                const char* modeStr = "STA>AP";  // default
                switch (currentMode) {
                    case 1: modeStr = "STA"; break;
                    case 2: modeStr = "AP"; break;
                    case 3: modeStr = "STA>AP"; break;
                }
                WebUI::wifi_on_mode->setStringValue((char*)modeStr);
            }
            WebUI::wifi_mode->setStringValue((char*)"Off");
            log_info("WiFi mode set to Off");
        }
    }

    if (has_hostname) {
        WebUI::wifi_hostname->setStringValue(hostname_val);
        log_info("WiFi hostname set from wifi_setup.txt: " << hostname_val);
    }

    // Delete the file
    if (remove(WIFI_SETUP_PATH) == 0) {
        log_info("wifi_setup.txt deleted from SD card");
    } else {
        log_warn("Failed to delete wifi_setup.txt");
    }

    // Special case: mode off and config already has WiFi off
    if (is_off && config->_wifiMode == 0) {
        wifi_setup_oled_msg("WiFi is already disabled by config.");
        return;  // no restart needed
    }

    // Show appropriate message and reboot on click
    if (is_off) {
        if (config_controls_mode) {
            wifi_setup_oled_msg("WiFi off requested, but mode is locked by config. Click to restart.");
        } else {
            wifi_setup_oled_msg("WiFi set to off. Click to restart.");
        }
    } else if (config->_wifiMode == 0) {
        wifi_setup_oled_msg("WiFi credentials saved. WiFi is disabled by config. Click to restart.");
    } else if (config_controls_mode) {
        // Check if credentials match the enforced mode
        bool mode_matches = (is_join && (config->_wifiMode == 1 || config->_wifiMode == 3)) ||
                           (is_hotspot && (config->_wifiMode == 2 || config->_wifiMode == 3));
        if (mode_matches) {
            wifi_setup_oled_msg("WiFi credentials updated. Click to restart.");
        } else {
            if (is_join) {
                wifi_setup_oled_msg("STA credentials saved (config uses AP). Click to restart.");
            } else {
                wifi_setup_oled_msg("AP credentials saved (config uses STA). Click to restart.");
            }
        }
    } else {
        // Config -1, join or hotspot
        if (is_join) {
            wifi_setup_oled_msg("WiFi network credentials updated. Click to restart.");
        } else {
            wifi_setup_oled_msg("WiFi hotspot credentials updated. Click to restart.");
        }
    }

    reboot_on_click();
}

#else
// No WiFi support compiled in
void check_wifi_setup_file() {}
#endif
