// Copyright (c) 2023 -	Matt Staniszewski

#pragma once

#include <cstdint>

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
        String      _url;
        int32_t     _read_period_ms;
    
        void        parse_titles(const String& rssData);
        void        fetch_and_parse();
    };

    extern RSSReader rssReader;
}
