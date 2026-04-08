// WifiSetupFile.h — Check for and process wifi_setup.txt on SD card
#pragma once

// Checks for wifi_setup.txt on SD card. If found, parses credentials,
// writes to NVS, shows OLED status, and reboots on user click.
// Must be called after SD card init and config loading,
// before WiFiConfig::begin().
void check_wifi_setup_file();
