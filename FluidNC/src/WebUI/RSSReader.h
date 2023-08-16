// Copyright (c) 2023 -	Matt Staniszewski

#pragma once

#include <cstdint>
#include "../OLED.h"
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

        bool            _started;
        String          _web_server;
        String          _web_rss_address;
        uint32_t        _refresh_period_sec;
        uint32_t        _refresh_start_ms;
        time_t          _last_update_time;
        time_t          _new_update_time;
        nvs_handle_t    _handle;

        bool            parse_server_address(std::string url, String *server, String *address);
        void            parse_item(tinyxml2::XMLElement *itemNode);
        int             parse_month_name(const char *monthName);
        time_t          parse_pub_date(const char *pubDate);
        void            fetch_and_parse();
    };

    extern RSSReader rssReader; 
    extern StringSetting* rss_url;
    extern IntSetting* rss_refresh_sec;
}
#endif
