// Copyright (c) 2023 -	Matt Staniszewski

#include "../Config.h"
#include "WifiConfig.h"  // wifi_config.Hostname()
#include "../Machine/MachineConfig.h"

#include "RSSReader.h"

namespace WebUI {
    RSSReader rssReader __attribute__((init_priority(106))) ;
}

namespace WebUI {

    StringSetting* rss_url;

    static const String DEFAULT_RSS_WEB_SERVER  = "mattstaniszewski.net";
    static const String DEFAULT_RSS_ADDRESS     = "/test_feed.xml";
    static const String DEFAULT_RSS_FULL_URL    = DEFAULT_RSS_WEB_SERVER + DEFAULT_RSS_ADDRESS;
    static const int MIN_RSS_URL = 0;
    static const int MAX_RSS_URL = 2083; // Based on Chrome, IE; other browsers allow more characters   

    static const int DEFAULT_RSS_WAIT_PERIOD_MS = 10000;

    // Constructor
    RSSReader::RSSReader() {
        _web_server         = DEFAULT_RSS_WEB_SERVER;
        _web_rss_address    = DEFAULT_RSS_ADDRESS;
        _wait_period_ms     = DEFAULT_RSS_WAIT_PERIOD_MS;
        _wait_start_time_ms = 0;
        _last_build_date    = 0;
        _started            = false;

        rss_url = new StringSetting("RSS URL", WEBSET, WA, NULL, "RSS/URL", DEFAULT_RSS_FULL_URL.c_str(), MIN_RSS_URL, MAX_RSS_URL, NULL);
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

        // Pull settings from WebUI if successful Wi-Fi connection
        if (res) {
            
            std::string url = std::string(rss_url->get());
            std::string host, path;

            // Parse the server and address from the URL
            size_t found = 0;

            // URL starts with http(s)
            if (url.rfind("http", 0) == 0) {
                found = url.find_first_of(":");
                found += 3; // Step over colon and slashes
            }
            
            size_t found1 = url.find_first_of(":", found);

            // Port found
            if (std::string::npos != found1) {
                host = url.substr(found, found1 - found);
                size_t found2 = url.find_first_of("/", found1);
                if (std::string::npos != found2) {
                    path = url.substr(found2);
                }

            // No port
            } else {
                found1 = url.find_first_of("/", found);
                if (std::string::npos != found1) {
                    host = url.substr(found, found1 - found);
                    path = url.substr(found1);
                }
            }

            _web_server = String(host.c_str());
            _web_rss_address = String(path.c_str());

            // Invalid URL, fail to start
            if ((_web_server.length() == 0) || (_web_rss_address.length() == 0)) {
                res = false;
            }
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
        _last_build_date    = 0;
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

    // Parses the items in RSS data
    void RSSReader::parse_item(tinyxml2::XMLElement *itemNode) {

        // Parse and process <title> and <link> elements
        const char *title = itemNode->FirstChildElement("title")->GetText();
        const char *link = itemNode->FirstChildElement("link")->GetText();
        
        // Print them out
        log_info("Title: " << title << ", Link: " << link);

         // Add the item to the RSS menu on screen
        config->_oled->_menu->add_rss_link(link, title);  // TODO
    }

    // Parses a three-letter month name into an integer
    int RSSReader::parse_month_name(const char *monthName) {

        if (strcmp(monthName, "Jan") == 0) return 0;
        if (strcmp(monthName, "Feb") == 0) return 1;
        if (strcmp(monthName, "Mar") == 0) return 2;
        if (strcmp(monthName, "Apr") == 0) return 3;
        if (strcmp(monthName, "May") == 0) return 4;
        if (strcmp(monthName, "Jun") == 0) return 5;
        if (strcmp(monthName, "Jul") == 0) return 6;
        if (strcmp(monthName, "Aug") == 0) return 7;
        if (strcmp(monthName, "Sep") == 0) return 8;
        if (strcmp(monthName, "Oct") == 0) return 9;
        if (strcmp(monthName, "Nov") == 0) return 10;
        if (strcmp(monthName, "Dec") == 0) return 11;
        
        return -1;  // Invalid month name
    }

    // Parses the last build date in RSS data
    time_t RSSReader::parse_last_build_date(const char *lastBuildDate) {

        struct tm tmStruct;
        memset(&tmStruct, 0, sizeof(tmStruct));
        
        // Parse date and time components from the lastBuildDate string
        sscanf(lastBuildDate, "%*3s, %d %3s %4d %2d:%2d:%2d %*3s",
                &tmStruct.tm_mday, lastBuildDate, &tmStruct.tm_year,
                &tmStruct.tm_hour, &tmStruct.tm_min, &tmStruct.tm_sec);
        
        tmStruct.tm_year -= 1900;  // Adjust year to be relative to 1900
        tmStruct.tm_mon = parse_month_name(lastBuildDate); // Implement parse_month_name
        
        // Convert the struct tm to a Unix timestamp
        return mktime(&tmStruct);
    }

    // Fetch an RSS feed and parse the data
    void RSSReader::fetch_and_parse() {
        
        bool insideItem = false;
        char c;
        String rssChunk = "";
        tinyxml2::XMLDocument rssDoc;
        tinyxml2::XMLElement *itemNode = nullptr;
        String lastBuildDate = "";

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

                    // Check for the last build date - assumes it is before items (as per RSS specification)
                    if (c == '>' && rssChunk.endsWith("</lastBuildDate>")) {

                        size_t start = rssChunk.lastIndexOf(">", rssChunk.indexOf("</lastBuildDate>"));
                        lastBuildDate = rssChunk.substring(start + 1, rssChunk.length() - 14); // Extract the date
                        
                        // Convert the last build date string to a time_t value
                        time_t timestamp = parse_last_build_date(lastBuildDate.c_str());
                        
                        // Print the last build date as a Unix timestamp
                        log_info("Last Build Date: " << (int32_t)timestamp);

                        // Feed is updated, notify and update listing
                        if (timestamp > _last_build_date) {
                            _last_build_date = timestamp;
                            log_info("*Feed is updated*");

                            // Prep the RSS menu on screen
                            config->_oled->_menu->prep_for_rss_update();
                        
                        // Feed hasn't changed, terminate RSS connection
                        } else {
                            rssClient.stop();
                            return;
                        }
                    }

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

            // Refresh the menu
            config->_oled->refresh_display(true);
        
        // Connection to RSS server failed
        } else {
            log_warn("Connection failed");
        }
        
        // End the RSS connection
        rssClient.stop();
    }
}