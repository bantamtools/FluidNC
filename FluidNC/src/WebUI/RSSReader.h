// Copyright (c) 2023 -	Matt Staniszewski

#pragma once

#include <cstdint>
#include <tinyxml2.h>
#include "../OLED.h"

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
        uint32_t    _wait_period_ms;
        uint32_t    _wait_start_time_ms;
        time_t      _last_build_date;
    
        void        parse_item(tinyxml2::XMLElement *itemNode);
        int         parse_month_name(const char *monthName);
        time_t      parse_last_build_date(const char *lastBuildDate);
        void        fetch_and_parse();
    };

    extern RSSReader rssReader;
}
