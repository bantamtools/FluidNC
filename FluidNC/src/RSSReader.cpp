// Copyright (c) 2023 -	Matt Staniszewski

#include "Config.h"

#include "RSSReader.h"
#include "Machine/MachineConfig.h"

// Constructor
RSSReader::RSSReader() {}

// Destructor
RSSReader::~RSSReader() {}

// RSS read task
void RSSReader::read_task(void *pvParameters) {

    // Connect pointer
    RSSReader* instance = static_cast<RSSReader*>(pvParameters);

    // Wait for Wi-Fi connection
    while (WiFi.status() != WL_CONNECTED) {
       vTaskDelay(1000/portTICK_PERIOD_MS); 
    }

    // Loop forever
    while(1) {

        // Fetch the RSS feed and parse data
        instance->fetch_and_parse();

        // Check every 10s
        vTaskDelay(RSS_READ_PERIODIC_MS/portTICK_PERIOD_MS);
    }
}

// Initializes the RSS reader subsystem
void RSSReader::init() {

    // Start read task
    xTaskCreate(read_task, "rss_read_task", RSS_READ_STACK_SIZE, this, RSS_READ_PRIORITY, NULL);

    // Print configuration info message
    log_info("RSS url:" << _url);
}

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
    http.begin("https://www.mattstaniszewski.net/test_feed.xml");

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        String rssData = http.getString();
        //log_info(rssData.c_str());
        parse_titles(rssData);
    } else {
        log_info("Failed to fetch RSS feed");
    }

    http.end();
}
