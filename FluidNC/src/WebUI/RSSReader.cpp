// Copyright (c) 2023 -	Matt Staniszewski

#include "../Config.h"
#include "WifiConfig.h"  // wifi_config.Hostname()
#include "../Machine/MachineConfig.h"

#include "RSSReader.h"

namespace WebUI {
    RSSReader rssReader __attribute__((init_priority(106))) ;
}

namespace WebUI {

    static const String DEFAULT_RSS_URL         = "https://www.mattstaniszewski.net/test_feed.xml";
    static const int DEFAULT_RSS_BOOT_WAIT_MS   = 30000;
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

            // Poll the XML data and refresh the list after wait expires or after boot delay
            if ((_wait_start_time_ms == 0 && (millis() < DEFAULT_RSS_BOOT_WAIT_MS)) || 
                ((millis() - _wait_start_time_ms) >= DEFAULT_RSS_WAIT_PERIOD_MS)) {

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

    // Parses the items in XML data
    void RSSReader::parse_item(tinyxml2::XMLElement *itemNode) {

        // Parse and process <title> and <link> elements
        const char *title = itemNode->FirstChildElement("title")->GetText();
        const char *link = itemNode->FirstChildElement("link")->GetText();
        
        // Print them out
        log_info("Title: " << title << ", Link: " << link);
    }

    // Fetch an RSS feed and parse the data
    void RSSReader::fetch_and_parse() {

        HTTPClient http;

        // Start the HTTP client and GET request
        http.begin(_url);
        int httpCode = http.GET(); // Fetch the content from the URL
        
        // OK response, process RSS data
        if (httpCode == HTTP_CODE_OK) {

            tinyxml2::XMLDocument xmlDoc;
            tinyxml2::XMLElement *itemNode = nullptr;
            String xmlChunk = "";
            char c;

            // Create stream for reading chunks (less memory)
            WiFiClient *stream = http.getStreamPtr();

            // Valid stream with data
            while (stream->connected() && stream->available()) {

                // Read a character and add to chunk
                c = stream->read();
                xmlChunk += c;

                // Check for the end of an item
                if (itemNode && c == '>' && xmlChunk.endsWith("</item>")) {

                    // Extract the <item> content from the chunk
                    size_t start = xmlChunk.indexOf("<item>");
                    size_t end = xmlChunk.indexOf("</item>") + 7; // Include the </item> tag
                    xmlChunk = xmlChunk.substring(start, end);

                    // Parse the extracted item content
                    if (xmlDoc.Parse(xmlChunk.c_str(), xmlChunk.length()) == tinyxml2::XML_SUCCESS) {

                        itemNode = xmlDoc.FirstChildElement("item");
                        parse_item(itemNode);
                        itemNode = nullptr;
                        xmlDoc.Clear();
                        
                    } else {
                        Serial.println("Failed to parse item XML");
                    }

                    // Clear the chunk
                    xmlChunk = "";

                }

                // Check for the start of a new item
                if (!itemNode && c == '>' && xmlChunk.endsWith("<item>")) {
                    itemNode = xmlDoc.NewElement("item");
                    xmlChunk = "<item>";
                }
            }
        
        // Error response
        } else {
            Serial.printf("HTTP request failed with code %d\n", httpCode);
        }

        // Close HTTP client 
        http.end();
    }
}