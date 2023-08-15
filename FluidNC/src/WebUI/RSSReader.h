// Copyright (c) 2023 -	Matt Staniszewski

#pragma once

#include <cstdint>
#include "../OLED.h"

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

        bool        begin();
        void        end();
        void        handle();
        bool        started();
        String      getUrl();
        
        ~RSSReader();

    private:

        bool        _started;
        String      _web_server;
        String      _web_rss_address;
        uint32_t    _refresh_period_sec;
        uint32_t    _refresh_start_ms;
        time_t      _last_build_date;

        void        parse_item(tinyxml2::XMLElement *itemNode);
        int         parse_month_name(const char *monthName);
        time_t      parse_last_build_date(const char *lastBuildDate);
        void        fetch_and_parse();
    };

    extern RSSReader rssReader; 
    extern StringSetting* rss_url;
    extern IntSetting* rss_refresh_sec;
}
#endif
