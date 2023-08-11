// Copyright (c) 2023 -	Matt Staniszewski

#include "../Config.h"
#include "WifiConfig.h"  // wifi_config.Hostname()
#include "../Machine/MachineConfig.h"

#include "RSSReader.h"

namespace WebUI {
    RSSReader rssReader __attribute__((init_priority(106))) ;
}

namespace WebUI {

    static const String DEFAULT_RSS_WEB_SERVER  = "mattstaniszewski.net";
    static const String DEFAULT_RSS_ADDRESS     = "/test_feed.xml";
    static const int DEFAULT_RSS_WAIT_PERIOD_MS = 10000;

    // Constructor
    RSSReader::RSSReader() {
        _web_server         = DEFAULT_RSS_WEB_SERVER;
        _web_rss_address    = DEFAULT_RSS_ADDRESS;
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

        _web_server         = "";
        _web_rss_address    = "";
        _wait_period_ms     = 0;
        _wait_start_time_ms = 0;
        _started            = false;
    }

    // Processes any RSS reader changes
    void RSSReader::handle() {
        if (_started) {

            // Poll the XML data and refresh the list after wait expires or after boot
            if ((_wait_start_time_ms == 0) || 
                ((millis() - _wait_start_time_ms) >= DEFAULT_RSS_WAIT_PERIOD_MS)) {

                // Parse XML data
                fetch_and_parse();

                // DEBUG: Print Heap
                //log_warn("Heap: " << xPortGetFreeHeapSize());

                // Set new wait start time
                _wait_start_time_ms = millis();
            }
        }
    }

    // Check if RSS reader service has been started
    bool RSSReader::started() { return _started; }

    // Returns the RSS URL
    String RSSReader::getUrl() { return (_web_server + _web_rss_address); }

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

        bool insideItem = false;
        char c;
        String rssChunk = "";
        tinyxml2::XMLDocument rssDoc;
        tinyxml2::XMLElement *itemNode = nullptr;

        // Set up RSS client (use instead of HTTPClient, takes more memory)
        WiFiClient rssClient;

        // Connected to RSS server
        if (rssClient.connect(_web_server.c_str(), 80)) {

            // GET Request
            rssClient.print("GET ");
            rssClient.print(_web_rss_address.c_str());
            rssClient.print(" HTTP/1.1\r\n");
            rssClient.print("Host: ");
            rssClient.print(_web_server.c_str());
            rssClient.print("\r\n");
            rssClient.print("Connection: close\r\n\r\n");

            // RSS data available
            while (rssClient.connected() || rssClient.available()) {

                while (rssClient.available()) {

                    // Read a byte at a time into RSS chunk
                    c = rssClient.read();
                    rssChunk += c;

                    // Check for the end of an item
                    if (itemNode && c == '>' && rssChunk.endsWith("</item>")) {

                        // Extract the <item> content from the chunk
                        size_t start = rssChunk.indexOf("<item>");
                        size_t end = rssChunk.indexOf("</item>") + 7; // Include the </item> tag
                        rssChunk = rssChunk.substring(start, end);

                        // Parse the extracted item content
                        if (rssDoc.Parse(rssChunk.c_str(), rssChunk.length()) == tinyxml2::XML_SUCCESS) {

                            itemNode = rssDoc.FirstChildElement("item");
                            parse_item(itemNode);
                            itemNode = nullptr;
                            rssDoc.Clear();

                        } else {
                            log_warn("Failed to parse item XML");
                        }
                        rssChunk = "";
                    }
                    
                    // Check for the start of a new item
                    if (!itemNode && c == '>' && rssChunk.endsWith("<item>")) {
                        itemNode = rssDoc.NewElement("item");
                        rssChunk = "<item>";
                    }
                }
            }
        
        // Connection to RSS server failed
        } else {
            log_warn("Connection failed");
        }
        
        // End the RSS connection
        rssClient.stop();
    }
}