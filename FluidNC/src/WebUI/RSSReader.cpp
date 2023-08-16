// Copyright (c) 2023 -	Matt Staniszewski

#ifndef ENABLE_WIFI
#include "RSSReader.h"
#include "../Machine/MachineConfig.h"

namespace WebUI {
    RSSReader rssReader;

    RSSReader::RSSReader() {}
    RSSReader::~RSSReader() { end(); }

    bool RSSReader::begin() { return false; }
    void RSSReader::end() {}
    void RSSReader::handle() {}
}
#else

#include "../Config.h"
#include "WifiConfig.h"  // wifi_config.Hostname()
#include "../Machine/MachineConfig.h"

#include "RSSReader.h"

namespace WebUI {
    RSSReader rssReader __attribute__((init_priority(106))) ;
}

namespace WebUI {

    StringSetting* rss_url;
    IntSetting* rss_refresh_sec;

    static const String DEFAULT_RSS_WEB_SERVER  = "mattstaniszewski.net";
    static const String DEFAULT_RSS_ADDRESS     = "/test_feed.xml";
    static const String DEFAULT_RSS_FULL_URL    = DEFAULT_RSS_WEB_SERVER + DEFAULT_RSS_ADDRESS;
    static const int MIN_RSS_URL = 0;
    static const int MAX_RSS_URL = 2083; // Based on Chrome, IE; other browsers allow more characters   

    static const int DEFAULT_RSS_REFRESH_SEC = 86400;   // 24 hours
    static const int MIN_RSS_REFRESH_SEC = 0;           // 0 = off
    static const int MAX_RSS_REFRESH_SEC = 604800;      // 1 week

    // Constructor
    RSSReader::RSSReader() {
        _web_server         = DEFAULT_RSS_WEB_SERVER;
        _web_rss_address    = DEFAULT_RSS_ADDRESS;
        _refresh_period_sec = DEFAULT_RSS_REFRESH_SEC;
        _refresh_start_ms   = 0;
        _last_update_time   = 0;
        _new_update_time    = 0;
        _started            = false;
        _handle             = 0;

        rss_url = new StringSetting("RSS URL", WEBSET, WA, NULL, "RSS/URL", DEFAULT_RSS_FULL_URL.c_str(), MIN_RSS_URL, MAX_RSS_URL, NULL);
        rss_refresh_sec = new IntSetting("RSS Refresh Time (s)", WEBSET, WA, NULL, "RSS/RefreshTimeSec", DEFAULT_RSS_REFRESH_SEC, MIN_RSS_REFRESH_SEC, MAX_RSS_REFRESH_SEC, NULL);
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

        // Get the last updated time from NVS
        if (res) {
            if (nvs_open("rss", NVS_READWRITE, &_handle) == ESP_OK) {
                esp_err_t ret = nvs_get_i32(_handle, "update_time", (int32_t*)&_last_update_time);
                if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
                    res = false;
                }
            } else {
                res = false;
            }
        }

        // Pull settings from WebUI if successful Wi-Fi connection
        if (res) {

            // Grab the refresh period
            _refresh_period_sec = rss_refresh_sec->get();

            // RSS feed disabled
            if (_refresh_period_sec == 0) {
                res = false;

            // RSS enabled, configure URL
            } else {
                
                // Parse the URL
                res = parse_server_address(rss_url->get(), &_web_server, &_web_rss_address);
            }
        }

        if (!res) {
            end();
        }
        _started = res;

        //TEMP
        download_file(NULL);
        
        return _started;
    }

    // Stop the RSS reader service
    void RSSReader::end() {
        if (!_started) {
            return;
        }

        _web_server         = "";
        _web_rss_address    = "";
        _refresh_period_sec = 0;
        _refresh_start_ms   = 0;
        _last_update_time   = 0;
        _new_update_time    = 0;
        _started            = false;

        // Close NVS handle
        nvs_close(_handle);  
        _handle = 0;
    }

    // Processes any RSS reader changes
    void RSSReader::handle() {
        
        if (_started) {

            // Poll the XML data and refresh the list after refresh period expires or after boot
            if ((_refresh_start_ms == 0) || 
                ((millis() - _refresh_start_ms) >= (_refresh_period_sec * 1000))) {

                // Parse XML data
                fetch_and_parse();

                // DEBUG: Print Heap
                //log_warn("Heap: " << xPortGetFreeHeapSize());

                // Set new refresh start time
                _refresh_start_ms = millis();
            }
        }
    }

    // Check if RSS reader service has been started
    bool RSSReader::started() { return _started; }

    // Returns the RSS URL
    String RSSReader::get_url() { return (_web_server + _web_rss_address); }

    // Destructor
    RSSReader::~RSSReader() { end(); }

    // Parses the server and address from a full URL
    bool RSSReader::parse_server_address(std::string url, String *server, String *address) {

        std::string host, path;
        size_t found = 0;
        bool res = true;

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

        *server = String(host.c_str());
        *address = String(path.c_str());

        // Invalid URL, fail to start
        if ((_web_server.length() == 0) || (_web_rss_address.length() == 0)) {
            res = false;
        }
        return res;
    }

    // Parses a three-letter month name into an integer
    int RSSReader::parse_month_name(const char *monthName) {

        if (strncmp(monthName, "Jan", 3) == 0) return 0;
        if (strncmp(monthName, "Feb", 3) == 0) return 1;
        if (strncmp(monthName, "Mar", 3) == 0) return 2;
        if (strncmp(monthName, "Apr", 3) == 0) return 3;
        if (strncmp(monthName, "May", 3) == 0) return 4;
        if (strncmp(monthName, "Jun", 3) == 0) return 5;
        if (strncmp(monthName, "Jul", 3) == 0) return 6;
        if (strncmp(monthName, "Aug", 3) == 0) return 7;
        if (strncmp(monthName, "Sep", 3) == 0) return 8;
        if (strncmp(monthName, "Oct", 3) == 0) return 9;
        if (strncmp(monthName, "Nov", 3) == 0) return 10;
        if (strncmp(monthName, "Dec", 3) == 0) return 11;
        
        return -1;  // Invalid month name
    }

    // Parses the published date for the given value
    time_t RSSReader::parse_pub_date(const char *pubDate) {

        struct tm tmStruct;
        memset(&tmStruct, 0, sizeof(tmStruct));

        // Copy the string so don't modify input
        char pubDateCopy[64];
        strncpy(pubDateCopy, pubDate, sizeof(pubDateCopy) - 1);
        pubDateCopy[sizeof(pubDateCopy) - 1] = '\0'; // Null-terminate the copy
        
        // Parse date and time components from the pubDate string
        sscanf(pubDateCopy, "%*3s, %d %3s %4d %2d:%2d:%2d %*3s",
                &tmStruct.tm_mday, pubDateCopy, &tmStruct.tm_year,
                &tmStruct.tm_hour, &tmStruct.tm_min, &tmStruct.tm_sec);
        
        tmStruct.tm_year -= 1900;  // Adjust year to be relative to 1900
        tmStruct.tm_mon = parse_month_name(pubDateCopy);
        
        // Convert the struct tm to a Unix timestamp
        return mktime(&tmStruct);
    }

    // Parses the items in RSS data
    void RSSReader::parse_item(tinyxml2::XMLElement *itemNode) {

        bool is_updated = false;

        // Parse and process <title> and <link> elements
        const char *title = itemNode->FirstChildElement("title")->GetText();
        const char *link = itemNode->FirstChildElement("link")->GetText();
        const char *pubDate = itemNode->FirstChildElement("pubDate")->GetText();

        // Convert pubDate to a timestamp and compare to last update
        time_t timestamp = parse_pub_date(pubDate);  // NOTE: Destructive, modifies

        // Newer than last NVS timestamp, flag update and save off value if newest
        if (timestamp > _last_update_time) {
            is_updated = true;
            if (timestamp > _new_update_time) {
                _new_update_time = timestamp;
            }
        }

        // Print elements out
        log_info("Title: " << title << ", Link: " << link << ", pubDate: " << itemNode->FirstChildElement("pubDate")->GetText() << ((is_updated) ? "*" : ""));

         // Add the item to the RSS menu on screen
        config->_oled->_menu->add_rss_link(link, title, is_updated);
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

                // Prep the RSS menu on screen
                config->_oled->_menu->prep_for_rss_update();

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

                // Once gone through all RSS entries, update the last update time to
                // the newest one available
                if (_new_update_time > _last_update_time) {
                    if (nvs_set_i32(_handle, "update_time", _new_update_time) == ESP_OK) {
                        _last_update_time = _new_update_time;
                    } else {
                        log_warn("Failed to store RSS update time in NVS!");
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

    // Downloads the specified RSS feed link to SD card
    void RSSReader::download_file(char *link) {

        WiFiClient download_client;

        // Connect to selected server
        if (download_client.connect("mattstaniszewski.net", 80)) {

            // Set no delay
            download_client.setNoDelay(1);

            // Make GET request for selected link, use keep-alive for faster download
            download_client.print("GET ");
            download_client.print("/rss/Apple.gcode");
            download_client.print(" HTTP/1.1\r\n");
            download_client.print("Host: ");
            download_client.print("mattstaniszewski.net");
            download_client.print("\r\n");
            download_client.print("Connection: keep-alive\r\n\r\n");

            // Wait for HTTP connection
            while (download_client.connected() && !download_client.available()) {
                delay(1);
            }

            // Determine the content length and skip rest of headers
            int content_length = 0;
            String content_length_header_name = "Content-Length: ";
            while (download_client.available()) {

                String line = download_client.readStringUntil('\n');
                line.trim(); // Remove whitespace
                
                // Save off content length (used for percent calculations)
                if (line.startsWith(content_length_header_name)) {
                    content_length = line.substring(content_length_header_name.length()).toInt();
                }

                // Read until find end of the headers (so we don't write them into our file)
                if (line.length() == 0) {
                    break;
                }
            }
            
            // Open the write file on SD
            FileStream *file = new FileStream("Apple.gcode", "w", "sd");
            if (file) {

                int bytes_read = 0;
                int total_bytes_read = 0;
                uint8_t buffer[1024];
                int i = 0;

                log_info("Starting download...");

                // Download file
                while (download_client.connected() || download_client.available()) {
                    if (download_client.available()) {
                        bytes_read = download_client.readBytes(buffer, sizeof(buffer));
                        file->write(buffer, bytes_read);
                        total_bytes_read += bytes_read;

                        // Calculate and print percent every 10 iterations (so we don't jam up log channel)
                        float percent = ((float)total_bytes_read / (float)content_length) * 100;
                        if (++i == 10) {
                            i = 0;
                            log_info("Downloaded: " << percent << "%");
                        }
                    }
                }
                delete(file);
                log_info("File saved to SD card");
            
            } else {
                log_info("Error opening file");
            }

        } else {
            log_info("Connection to server failed");
        }

        // Close the connection
        download_client.stop();
    }
}
#endif