// Copyright (c) 2023 -	Matt Staniszewski

#pragma once

#include <cstdint>
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
        uint32_t    _wait_period_ms;
        uint32_t    _wait_start_time_ms;
    
        void        parse_item(tinyxml2::XMLElement *itemNode);
        void        fetch_and_parse();
    };

    extern RSSReader rssReader;
}
