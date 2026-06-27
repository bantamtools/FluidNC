// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

//Preferences entries

#include "../Config.h"       // ENABLE_*
#include "../Channel.h"      // Channel
#include "../Error.h"        // Error
#include "Authentication.h"  // AuthenticationLevel

#include <atomic>

#ifndef ENABLE_WIFI
namespace WebUI {
    class WiFiConfig {
    public:
        static std::string webInfo() { return std::string(); }
        static std::string station_info() { return std::string(); }
        static std::string ap_info() { return std::string(); }

        static bool isPasswordValid(const char* password) { return false; }
        static bool begin() { return false; }
        static void end() {}
        static void reset() {}
        static void reset_settings() {}
        static void handle() {}
        static bool isOn() { return false; }
        static bool sta_got_ip() { return false; }
        static void showWifiStats(Channel& out) {}
        // ( review A2) keep the non-WIFI stub in parity with the real class so
        // Protocol.cpp / WebSettings.cpp (which call these at depth 0) still compile.
        static void requestEnd() {}
        static void lifecycleService() {}
    };
    extern WiFiConfig wifi_config;
}
#else
#    include <WiFi.h>
#    include "../Settings.h"

namespace WebUI {
    enum WiFiStartupMode {
        WiFiOff = 0,
        WiFiSTA,
        WiFiAP,
        WiFiFallback,  // Try STA, fall back to AP if STA fails
    };

    extern StringSetting* wifi_hostname;

    static const int DHCP_MODE   = 0;
    static const int STATIC_MODE = 1;

    //defaults values
    static const char* DEFAULT_HOSTNAME   = "BantamArtFrame"; // note max 32 char // TODO: better build integration to set this per machine type?
    static const char* DEFAULT_STA_SSID   = "";
    static const char* DEFAULT_STA_PWD    = "";
    static const char* DEFAULT_STA_IP     = "0.0.0.0";
    static const char* DEFAULT_STA_GW     = "0.0.0.0";
    static const char* DEFAULT_STA_MK     = "0.0.0.0";
    static const char* DEFAULT_AP_SSID    = "Bantam";
    static const char* DEFAULT_AP_PWD     = "12345678";
    static const char* DEFAULT_AP_IP      = "192.168.0.1";
    static const char* DEFAULT_AP_MK      = "255.255.255.0";
    static const int   DEFAULT_AP_CHANNEL = 1;

    static const int   DEFAULT_STA_MIN_SECURITY = WIFI_AUTH_WPA2_PSK;
    static const int   DEFAULT_STA_IP_MODE      = DHCP_MODE;
    static const char* HIDDEN_PASSWORD          = "********";

    //boundaries
    static const int MAX_SSID_LENGTH     = 32;
    static const int MIN_SSID_LENGTH     = 0;  // Allow null SSIDs as a way to disable
    static const int MAX_PASSWORD_LENGTH = 64;
    //min size of password is 0 or upper than 8 char
    //so let set min is 8
    static const int MIN_PASSWORD_LENGTH = 8;
    static const int MAX_HOSTNAME_LENGTH = 32;
    static const int MIN_HOSTNAME_LENGTH = 1;
    static const int MIN_CHANNEL         = 1;
    static const int MAX_CHANNEL         = 14;

    class WiFiConfig {
    public:
        WiFiConfig();

        static void reset();

        static std::string webInfo();

        static std::string station_info();
        static std::string ap_info();

        static bool isValidIP(const char* string);
        static bool isPasswordValid(const char* password);
        static bool isHostnameValid(const char* hostname);

        static std::string Hostname() { return _hostname; }
        static std::string Hostname_Setting() { return wifi_hostname->get(); }

        static char*   mac2str(uint8_t mac[8]);
        static bool    StartAP();
        static bool    StartSTA();
        static void    StopWiFi();
        static int32_t getSignal(int32_t RSSI);
        static bool    begin();
        static void    end();
        static void    handle();
        static void    reset_settings();
        static bool    isOn();

        // ( Stage 2) WiFi-service lifecycle is owned by the wifi_task (it is the
        // sole task that runs wifi_services.handle()/end()). Cross-task callers must
        // NOT delete the servers out from under a wifi_task that may be mid-handle().
        // requestEnd() is FIRE-AND-FORGET: boot/no-task runs end() directly; otherwise
        // it latches a request the wifi_task executes at lifecycleService() (its loop
        // top, before handle()). Never blocks the caller -> core-1 (motion) is never
        // stalled by a parked WiFi socket op. Runtime WiFi-ON always reboots, so begin()
        // /StartSTA() only ever run at boot (pre-task) or self (wifi_task) -> no
        // cross-task begin() primitive is needed.
        static void    requestEnd();
        static void    lifecycleService();  // wifi_task loop top: execute any latched end()

        static bool    sta_got_ip() { return _sta_got_ip; }
        //  True between an STA disconnect and the next GOT_IP. While set, the
        // network channel writers (WS/telnet) skip their writes, so a broadcast
        // can't park ~1.3-10s in lwIP send() on a half-open socket (no FIN/RST yet).
        // Lock-free: one writer (WiFi-event task) stores, the core-0 comms tasks load.
        static bool staTxSuspect() { return _sta_tx_suspect.load(std::memory_order_relaxed); }

        static Error listAPs(char* parameter, AuthenticationLevel auth_level, Channel& out);
        static void  showWifiStats(Channel& out);

        ~WiFiConfig();

    private:
        static bool ConnectSTA2AP();
        static void WiFiEvent(arduino_event_id_t event, arduino_event_info_t info);
        static bool _events_registered;
        static volatile bool _sta_got_ip;
        static std::atomic<bool> _sta_tx_suspect;  //  STA disconnected -> suppress net-channel writes
        static std::atomic<bool> _endRequested;    // ( Stage 2) deferred WiFi-off latch

        static std::string _hostname;
    };

    extern WiFiConfig wifi_config;

    extern EnumSetting* wifi_mode;
    extern EnumSetting* wifi_on_mode;
    extern StringSetting* wifi_config_name;

    extern StringSetting* wifi_sta_ssid;
    extern StringSetting* wifi_sta_password;

    extern EnumSetting*   wifi_fast_scan;
    extern EnumSetting*   wifi_sta_min_security;
    extern EnumSetting*   wifi_sta_mode;
    extern IPaddrSetting* wifi_sta_ip;
    extern IPaddrSetting* wifi_sta_gateway;
    extern IPaddrSetting* wifi_sta_netmask;

    extern StringSetting* wifi_ap_ssid;
    extern StringSetting* wifi_ap_password;

    extern IPaddrSetting* wifi_ap_ip;

    extern IntSetting* wifi_ap_channel;

    extern StringSetting* wifi_hostname;
}
#endif