// Copyright (c) 2023 -	Matt Staniszewski

#pragma once

#include <limits>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include "../OLED.h"
#include "../DownloadFile.h"
#include <nvs.h>

#ifndef ENABLE_WIFI

namespace WebUI {
    class RSSReader {
    public:
        RSSReader();

        bool        begin();
        void        end();
        void        handle();
        
        ~RSSReader();
    };

    extern RSSReader rssReader; 
}
#else

#include <tinyxml2.h>

namespace WebUI {
    class RSSReader {
    public:
        RSSReader();

        bool            begin();
        void            end();
        void            handle();
        bool            started();
        String          get_url();
        void            download_file(char *link);
        
        ~RSSReader();

    private:

        static constexpr UBaseType_t    RSS_FETCH_PRIORITY      = (configMAX_PRIORITIES - 4);
        static constexpr uint32_t       RSS_FETCH_STACK_SIZE    = 4096;
        static constexpr uint32_t       RSS_FETCH_PERIODIC_MS   = 100;

        bool            _started;
        String          _web_server;
        String          _web_rss_address;
        uint32_t        _refresh_period_sec;
        uint32_t        _refresh_start_ms;
        time_t          _last_update_time;
        time_t          _new_update_time;
        nvs_handle_t    _handle;
        bool            _refresh_rss;

        bool            parse_server_address(const String url, String *server, String *address);
        void            parse_item(tinyxml2::XMLElement *itemNode);
        int             parse_month_name(const char *monthName);
        time_t          parse_pub_date(const char *pubDate);
        static void     fetch_and_parse_task(void *pvParameters);
    };

    extern RSSReader rssReader; 
    extern StringSetting* rss_url;
    extern IntSetting* rss_refresh_sec;
}
#endif
