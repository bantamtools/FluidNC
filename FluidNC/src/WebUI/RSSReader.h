// Copyright (c) 2023 -	Matt Staniszewski

#pragma once

#include <limits>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include "../List.h"
#include "../OLED.h"
#include "../DownloadFile.h"
#include <nvs.h>
#include <WiFiClientSecure.h>

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
    class RSSReader : public List {
    public:
        RSSReader();

        bool            begin();
        void            end();
        void            handle();
        bool            started();
        String          get_url();
        void            download_file(char *link, char *filename);
        time_t          get_last_update_time() { return _last_update_time; };
        void            sync() { _refresh_start_ms = 0; };
        ListNodeType    *get_rss_feed() { return _rss_feed->head; };

        ~RSSReader();

    private:

        static constexpr uint32_t       RSS_FETCH_PERIODIC_MS   = 100;

        ListType        *_rss_feed;

        bool            _started;
        bool            _valid_feed;
        int             _num_entries;
        String          _web_server;
        String          _web_rss_address;
        uint32_t        _refresh_period_sec;
        uint32_t        _refresh_start_ms;
        time_t          _last_update_time;
        time_t          _new_update_time;
        nvs_handle_t    _handle;

        bool            parse_server_address(const String url, String *server, String *address);
        void            parse_item(tinyxml2::XMLElement *itemNode);
        int             parse_month_name(const char *monthName);
        time_t          parse_pub_date(const char *pubDate);
        void            fetch_and_parse_feed();
    };

    extern RSSReader rssReader; 
    extern StringSetting* rss_url;
    extern IntSetting* rss_refresh_sec;
}
#endif
