// Copyright (c) 2023 -	Matt Staniszewski

#include "../Config.h"
#include "WifiConfig.h"  // wifi_config.Hostname()
#include "../Machine/MachineConfig.h"

#include "RSSReader.h"

namespace WebUI {
    RSSReader rssReader __attribute__((init_priority(106))) ;
}

namespace WebUI {

    static const String DEFAULT_RSS_URL = "https://www.mattstaniszewski.net/test_feed.xml";
    static const int DEFAULT_RSS_WAIT_PERIOD_MS = 10000;

    // Constructor
    RSSReader::RSSReader() {
        _url                = DEFAULT_RSS_URL;
        _wait_period_ms     = DEFAULT_RSS_WAIT_PERIOD_MS;
        _wait_start_time_ms = 0;
        _started            = false;
    }

    // Starts the RSS reader subsystem
    bool RSSReader::begin() {

        // Stop if previously running
        end();

        // Check that Wi-Fi is running
        bool res = true;
        if ((WiFi.getMode() != WIFI_MODE_STA) && (WiFi.getMode() != WIFI_MODE_APSTA)) {
            res = false;
        }
        if (!res) {
            end();
        }
        _started = res;
        return _started;
    }

    // Stop the RSS reader service
    void RSSReader::end() {
        if (!_started) {
            return;
        }

        _url                = "";
        _wait_period_ms     = 0;
        _wait_start_time_ms = 0;
        _started            = false;
    }

    // Processes any RSS reader changes
    void RSSReader::handle() {
        if (_started) {

            // Poll the XML data and refresh the list after wait expires or at start
            if ((_wait_start_time_ms == 0) || ((millis() - _wait_start_time_ms) >= DEFAULT_RSS_WAIT_PERIOD_MS)) {

                // Parse XML data
                fetch_and_parse();

                // Print Heap
                log_warn("Heap: " << xPortGetFreeHeapSize());

                // Set new wait start time
                _wait_start_time_ms = millis();
            }
        }
    }

    // Check if RSS reader service has been started
    bool RSSReader::started() { return _started; }

    // Returns the RSS URL
    String RSSReader::getUrl() { return _url; }

    // Destructor
    RSSReader::~RSSReader() { end(); }

    // Parses the titles in XML data
    void RSSReader::parse_titles(const String& rssData) {

        tinyxml2::XMLDocument xml;
        if (xml.Parse(rssData.c_str()) == tinyxml2::XML_SUCCESS) {
            tinyxml2::XMLElement* channel = xml.FirstChildElement("rss")->FirstChildElement("channel");
            tinyxml2::XMLElement* item = channel->FirstChildElement("item");

            while (item) {
                const char* title = item->FirstChildElement("title")->GetText();
                log_info(title);
                item = item->NextSiblingElement("item");
            }
        } else {
            log_error("XML parsing error");
        }
    }

    // Fetch an RSS feed and parse the data
    void RSSReader::fetch_and_parse() {

        HTTPClient http;
        http.begin(_url);

        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            String rssData = http.getString();
            parse_titles(rssData);
        } else {
            log_info("Failed to fetch RSS feed");
        }

        http.end();
    }
}