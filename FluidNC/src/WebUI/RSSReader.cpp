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

const char* dropbox_root_ca= \
    "-----BEGIN CERTIFICATE-----\n" \
    "MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh\n" \
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n" \
    "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD\n" \
    "QTAeFw0wNjExMTAwMDAwMDBaFw0zMTExMTAwMDAwMDBaMGExCzAJBgNVBAYTAlVT\n" \
    "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n" \
    "b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IENBMIIBIjANBgkqhkiG\n" \
    "9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4jvhEXLeqKTTo1eqUKKPC3eQyaKl7hLOllsB\n" \
    "CSDMAZOnTjC3U/dDxGkAV53ijSLdhwZAAIEJzs4bg7/fzTtxRuLWZscFs3YnFo97\n" \
    "nh6Vfe63SKMI2tavegw5BmV/Sl0fvBf4q77uKNd0f3p4mVmFaG5cIzJLv07A6Fpt\n" \
    "43C/dxC//AH2hdmoRBBYMql1GNXRor5H4idq9Joz+EkIYIvUX7Q6hL+hqkpMfT7P\n" \
    "T19sdl6gSzeRntwi5m3OFBqOasv+zbMUZBfHWymeMr/y7vrTC0LUq7dBMtoM1O/4\n" \
    "gdW7jVg/tRvoSSiicNoxBN33shbyTApOB6jtSj1etX+jkMOvJwIDAQABo2MwYTAO\n" \
    "BgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUA95QNVbR\n" \
    "TLtm8KPiGxvDl7I90VUwHwYDVR0jBBgwFoAUA95QNVbRTLtm8KPiGxvDl7I90VUw\n" \
    "DQYJKoZIhvcNAQEFBQADggEBAMucN6pIExIK+t1EnE9SsPTfrgT1eXkIoyQY/Esr\n" \
    "hMAtudXH/vTBH1jLuG2cenTnmCmrEbXjcKChzUyImZOMkXDiqw8cvpOp/2PV5Adg\n" \
    "06O/nVsJ8dWO41P0jmP6P6fbtGbfYmbW0W5BjfIttep3Sp+dWOIrWcBAI+0tKIJF\n" \
    "PnlUkiaY4IBIqDfv8NZ5YBberOgOzW6sRBc4L0na4UU+Krk2U886UAb3LujEV0ls\n" \
    "YSEY1QSteDwsOoBrp+uvFRTp2InBuThs4pFsiv9kuXclVzDAGySj4dzp30d8tbQk\n" \
    "CAUw7C29C79Fv1C5qfPrmAESrciIxpg0X40KPMbp1ZWVbd4=\n" \
    "-----END CERTIFICATE-----\n";

namespace WebUI {
    RSSReader rssReader __attribute__((init_priority(110))) ;
}

namespace WebUI {

    StringSetting* rss_url;
    IntSetting* rss_refresh_sec;

    static const String DEFAULT_RSS_WEB_SERVER  = "rss.bantamtools.com";
    static const String DEFAULT_RSS_ADDRESS     = "/";
    static const String DEFAULT_RSS_FULL_URL    = DEFAULT_RSS_WEB_SERVER + DEFAULT_RSS_ADDRESS;
    static const int MIN_RSS_URL = 0;
    static const int MAX_RSS_URL = 2083; // Based on Chrome, IE; other browsers allow more characters   

    static const int DEFAULT_RSS_REFRESH_SEC = 86400;   // 24 hours
    static const int MIN_RSS_REFRESH_SEC = 0;           // 0 = off
    static const int MAX_RSS_REFRESH_SEC = 604800;      // 1 week

    // Constructor
    RSSReader::RSSReader() {

        _rss_feed           = new struct ListType;
        _web_server         = DEFAULT_RSS_WEB_SERVER;
        _web_rss_address    = DEFAULT_RSS_ADDRESS;
        _refresh_period_sec = DEFAULT_RSS_REFRESH_SEC;
        _refresh_start_ms   = 0;
        _last_update_time   = 0;
        _new_update_time    = 0;
        _started            = false;
        _valid_feed         = false;
        _num_entries        = 0;
        _handle             = 0;
        _refresh_rss        = false;

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

        // Initialize the feed and connect to menu (if available)
        if (res) {

            if (config->_oled) {
                config->_oled->_menu->connect_rss_feed(_rss_feed);
            } else {
                init(_rss_feed, NULL);
            }
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
                res = parse_server_address(String(rss_url->get()), &_web_server, &_web_rss_address);
            }
        }

        if (!res) {
            end();
        }
        _started = res;
        
        // Fork a task for parsing XML data
        if (res) {
            xTaskCreate(fetch_and_parse_task, "fetch_and_parse_task", RSS_FETCH_STACK_SIZE, this, RSS_FETCH_PRIORITY, NULL);
        }
        
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
        _valid_feed         = false;
        _num_entries        = 0;
        _refresh_rss        = false;

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

                // Ensure the WebUI gets the updates first (if available)
                // before we refresh and set the new update time
                delay_ms(100);

                // Flag an RSS refresh
                _refresh_rss = true;

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
    RSSReader::~RSSReader() { 
        
        end(); 

        // Remove all list nodes and deallocate memory
        remove_entries(_rss_feed);
        delete(_rss_feed);    
    }

    // Parses the server and address from a full URL
    bool RSSReader::parse_server_address(const String url, String *server, String *address) {

        int found = 0;
        bool res = true;

        // URL starts with http(s)
        if (url.lastIndexOf("http", 0) == 0) {
            found = url.indexOf(":");
            found += 3; // Step over colon and slashes
        }
        
        int found1 = url.indexOf(":", found);

        // Port found
        if (found1 >= 0) {
            *server = url.substring(found, found1);
            int found2 = url.indexOf("/", found1);
            if (found2 >= 0) {
                *address = url.substring(found2);
            }

        // No port
        } else {
            found1 = url.indexOf("/", found);
            if (found1 >= 0) {
                *server = url.substring(found, found1);
                *address = url.substring(found1);
            // Special case with no trailing slash
            } else {
                *server = url;
                *address = "/";
            }
        }

        // Invalid URL, fail
        if ((server->length() == 0) || (address->length() == 0)) {
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

        const char *title = NULL;
        const char *link = NULL;
        const char *pubDate = NULL;
        time_t timestamp = -1;
        bool is_updated = false;

        // Parse and process elements
        if (itemNode->FirstChildElement("title")) { 
            title = itemNode->FirstChildElement("title")->GetText();
        }
        if (itemNode->FirstChildElement("link")) {
            link = itemNode->FirstChildElement("link")->GetText();
        }
        if (itemNode->FirstChildElement("pubDate")) {
            pubDate = itemNode->FirstChildElement("pubDate")->GetText();
        }

        // Convert pubDate to a timestamp and compare to last update
        if (pubDate) {
            timestamp = parse_pub_date(pubDate);  // NOTE: Destructive, modifies
        }

        // Check for invalid items
        if (!title || !link || !pubDate || (timestamp < 0)) {
            _valid_feed = false;
            return;
        }

        // Newer than last NVS timestamp, flag update and save off value if newest
        if (timestamp > _last_update_time) {
            is_updated = true;
            if (timestamp > _new_update_time) {
                _new_update_time = timestamp;
            }
        }

        // Print elements out
        //log_info("Title: " << title << ", Link: " << link << ", pubDate: " << itemNode->FirstChildElement("pubDate")->GetText() << ((is_updated) ? "*" : ""));

        // Add the item to the RSS feed
        add_entry(_rss_feed, NULL, link, title, is_updated);

        // Valid item, flag true and increment count
        _valid_feed = true;
        _num_entries++;
    }

    // Fetch an RSS feed and parse the data
    void RSSReader::fetch_and_parse_task(void *pvParameters) {
        
#ifdef DEBUG_MEMORY_WATERMARKS
        uint32_t start_time = millis();
#endif

        // Connect pointer
        RSSReader* instance = static_cast<RSSReader*>(pvParameters);

        // Loop forever
        while(1) {

            // Wait for flag to trigger
            if (instance->_refresh_rss) {

                // Instantiate variables
                String *rssChunk = new String;
                tinyxml2::XMLDocument *rssDoc = new tinyxml2::XMLDocument;
                tinyxml2::XMLElement *itemNode = nullptr;

                // Set up RSS client (use instead of HTTPClient, takes more memory)
                WiFiClient *rssClient = new WiFiClient;

                // Clear flag
                instance->_refresh_rss = false;

                // Set flag to start with valid feed and clear entry count
                instance->_valid_feed = true;
                instance->_num_entries = 0;

                // Prep the RSS feed list
                instance->prep(instance->_rss_feed);

                // Connected to RSS server
                if (rssClient->connect(instance->_web_server.c_str(), 80)) {

                    // GET Request
                    rssClient->print("GET ");
                    rssClient->print(instance->_web_rss_address.c_str());
                    rssClient->print(" HTTP/1.1\r\n");
                    rssClient->print("Host: ");
                    rssClient->print(instance->_web_server.c_str());
                    rssClient->print("\r\n");
                    rssClient->print("Connection: close\r\n\r\n");
                    
                    while ((rssClient->connected() || rssClient->available()) && instance->_valid_feed) {

                        while (rssClient->available() && instance->_valid_feed) {

                            // Read a RSS data and append locally to RSS chunk
                            *rssChunk += rssClient->readString();

                            // Parse the items in the RSS chunk
                            while((rssChunk->indexOf("<item>") >= 0) || (rssChunk->indexOf("</item>") >= 0)) {

                                // Check for the start of a new item
                                if (!itemNode && rssChunk->indexOf("<item>") >= 0) {
                                    itemNode = rssDoc->NewElement("item");
                                }

                                // Check for the end of an item
                                if (itemNode && rssChunk->indexOf("</item>") >= 0) {

                                    // Extract the <item> content from the chunk
                                    size_t start = rssChunk->indexOf("<item>");
                                    size_t end = rssChunk->indexOf("</item>") + 7; // Include the </item> tag
                                    log_debug("RSS Item: " << rssChunk->substring(start, end).c_str());

                                    // Parse the extracted item content
                                    if (rssDoc->Parse(rssChunk->substring(start, end).c_str(), rssChunk->substring(start, end).length()) == tinyxml2::XML_SUCCESS) {

                                        itemNode = rssDoc->FirstChildElement("item");
                                        instance->parse_item(itemNode);
                                        itemNode = nullptr;
                                        rssDoc->Clear();

                                    } else {
                                        instance->_valid_feed = false;
                                        log_warn("Failed to parse item XML");
                                    }

                                    // Remove everything up to the item content from the chunk
                                    rssChunk->remove(0, end);
                                }
                            }
                        }

                        // Once gone through all RSS entries, update the last update time to
                        // the newest one available and popup message about new updates
                        if (instance->_new_update_time > instance->_last_update_time) {
                            if (nvs_set_i32(instance->_handle, "update_time", instance->_new_update_time) == ESP_OK) {
                                instance->_last_update_time = instance->_new_update_time;
                            } else {
                                log_warn("Failed to store RSS update time in NVS!");
                            }

                            if (config->_oled) {
                                config->_oled->popup_msg("New RSS updates!", 5000);
                            }
                        }

                        // Check every 100ms
                        vTaskDelay(RSS_FETCH_PERIODIC_MS/portTICK_PERIOD_MS);
                    }

                    // Bad RSS URL or format
                    if ((instance->_num_entries == 0) || (!instance->_valid_feed)) {

                        // Report error
                        instance->prep(instance->_rss_feed);
                        instance->add_entry(instance->_rss_feed, NULL, "ERROR", "Error: Bad URL/format", false);

                        // Print error message to display
                        if (config->_oled) {
                            config->_oled->refresh_display(true);
                        }

                        log_rss("Error: Bad URL or format");

                    } else {

                        // Refresh the menu
                        if (config->_oled) {
                            config->_oled->refresh_display(true);
                        }
                
                        log_rss("Fetch completed");
                    }
                
                // Connection to RSS server failed
                } else {

                    // Report error
                    instance->add_entry(instance->_rss_feed, NULL, "ERROR", "Error: Connection failed", false);

                    // Print error message to display
                    if (config->_oled) {
                        config->_oled->refresh_display(true);
                    }
                    
                    log_rss("Error: Connection failed");
                }
                
                // End the RSS connection
                rssClient->stop();

                // Free memory
                delete(rssClient);
                delete(rssDoc);  // Also deletes all itemNodes
                delete(rssChunk);
            }

#ifdef DEBUG_MEMORY_WATERMARKS
            if (millis() - start_time >= DEBUG_MEMORY_WM_TIME_MS) {
                log_warn("rss_fetch_and_parse_task watermark -> " << uxTaskGetStackHighWaterMark(NULL));
                start_time = millis();
            }
#endif

            // Check every 100ms
            vTaskDelay(RSS_FETCH_PERIODIC_MS/portTICK_PERIOD_MS);
        }
    }

    // Downloads the specified RSS feed link to SD card
    void RSSReader::download_file(char *link, char *filename) {

        WiFiClientSecure download_client;
        String server, address;

        // Check for SD card, send message and return on fail
        if (!sd_card_is_present()) {
            if (config->_oled) {
                config->_oled->popup_msg("Please insert SD card");
            }
            return;
        }
        
        // Parse the URL, return on fail
        if(!parse_server_address(String(link), &server, &address)) {
            return;
        }

        // Set the client certificate for HTTPS
        download_client.setCACert(dropbox_root_ca);

        // Connect to selected server
        if (download_client.connect(server.c_str(), 443)) {

            // Set no delay
            download_client.setNoDelay(1);

            // Make HEAD request to get the content length (Dropbox doesn't always honor in GET requests)
            download_client.print("HEAD ");
            download_client.print(address.c_str());
            download_client.print(" HTTP/1.1\r\n");
            download_client.print("Host: ");
            download_client.print(server.c_str());
            download_client.print("\r\n\r\n");

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

            // Make GET request for selected link, use keep-alive for faster download
            download_client.print("GET ");
            download_client.print(address.c_str());
            download_client.print(" HTTP/1.1\r\n");
            download_client.print("Host: ");
            download_client.print(server.c_str());
            download_client.print("\r\n");
            download_client.print("Connection: keep-alive\r\n\r\n");

            // Wait for HTTP connection
            while (download_client.connected() && !download_client.available()) {
                delay(1);
            }

            // Skip the headers
            while (download_client.available()) {

                String line = download_client.readStringUntil('\n');
                line.trim(); // Remove whitespace

                // Read until find end of the headers (so we don't write them into our file)
                if (line.length() == 0) {
                    break;
                }
            }
            
            // Open the write file on SD
            DownloadFile *file = new DownloadFile(filename, content_length, allChannels);
            if (file) {

                int bytes_read = 0;
                int total_bytes = 0;
                uint8_t buffer[1024];

                // Download file
                log_info("File download started");
                while (download_client.connected() || download_client.available()) {
                    if (download_client.available()) {
                        bytes_read = download_client.readBytes(buffer, sizeof(buffer));
                        file->write(buffer, bytes_read);
                        total_bytes += bytes_read;
                        if (total_bytes == content_length) break;  // Workaround for Dropbox not auto-closing after file completes
                    }
                }
                delete(file);

                // Update the files list on SD card and exit to main menu
                sd_populate_files_menu();
                if (config->_oled) {
                    config->_oled->_menu->exit_submenu();
                }
                log_info("File download completed");

            } else {
                log_warn("Error opening file");
            }

        } else {
            log_warn("Connection to server failed");
        }

        // Close the connection
        download_client.stop();
    }
}
#endif