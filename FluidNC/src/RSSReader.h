// Copyright (c) 2023 -	Matt Staniszewski

#pragma once

#include "Configuration/Configurable.h"
#include "Pin.h"

#include <cstdint>

class RSSReader : public Configuration::Configurable {
public:

private:

    static constexpr UBaseType_t    RSS_READ_PRIORITY       = (configMAX_PRIORITIES - 3);
    static constexpr uint32_t       RSS_READ_STACK_SIZE     = 4096;
    static constexpr uint32_t       RSS_READ_PERIODIC_MS    = 10000;

    static void read_task(void *pvParameters);

    std::string _url;

public:
    RSSReader();
    RSSReader(const RSSReader&) = delete;
    RSSReader& operator=(const RSSReader&) = delete;

    void init();
    void parse_titles(const String& rssData);
    void fetch_and_parse();

    // Configuration handlers.
    void group(Configuration::HandlerBase& handler) override {
        handler.item("url", _url);
    }

    ~RSSReader();
};
