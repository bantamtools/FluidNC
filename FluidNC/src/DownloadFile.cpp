// Copyright (c) 2023 -	Matt Staniszewski, Bantam Tools

#include "DownloadFile.h"
#include "Report.h"

DownloadFile::DownloadFile(const char* link, const char* title, Channel& out) :
    FileStream(title, "w", "sd"), _link(String(link)), _total_bytes_read(0), _content_length(0), _out(out) {
    
    // Set autoreporting to 250ms
    setReportInterval(250);

    // Start download
    begin();
}

void DownloadFile::autoReport() {
    if (_reportInterval) {
        if ((int32_t(xTaskGetTickCount()) - _nextReportTime) >= 0) {
            _nextReportTime = xTaskGetTickCount() + _reportInterval;
            report_realtime_status(_out);
        }
    }
}

// return a percentage complete 50.5 = 50.5%
float DownloadFile::percent_complete() {
   
    return (float)_total_bytes_read / (float)_content_length * 100.0f;
}

std::string DownloadFile::_progress = "";

#include <sstream>
#include <iomanip>

size_t DownloadFile::write(const uint8_t* buffer, size_t length) {

    size_t bytes_read;
    std::ostringstream s;
    
    // Write out the buffer
    bytes_read = FileStream::write(buffer, length);
    _total_bytes_read += bytes_read;
    
    // Report status to the channel
    s << "DL:" << std::fixed << std::setprecision(2) << percent_complete() << "," << path().c_str();
    _progress = s.str();
    autoReport();

    return bytes_read;
}

DownloadFile::~DownloadFile() {
    _progress = "";
}

void DownloadFile::begin() {

    WiFiClient download_client;
    String server, address;
    int bytes_read = 0;
    uint8_t buffer[1024];

    // Parse the URL, return on fail
    if (!WebUI::rssReader.parse_server_address(_link, &server, &address)) {
        return;
    }

    // Connect to selected server
    if (download_client.connect(server.c_str(), 80)) {

        // Set no delay
        download_client.setNoDelay(1);

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

        // Determine the content length and skip rest of headers
        String content_length_header_name = "Content-Length: ";
        while (download_client.available()) {

            String line = download_client.readStringUntil('\n');
            line.trim(); // Remove whitespace
            
            // Save off content length (used for percent calculations)
            if (line.startsWith(content_length_header_name)) {
                _content_length = line.substring(content_length_header_name.length()).toInt();
            }

            // Read until find end of the headers (so we don't write them into our file)
            if (line.length() == 0) {
                break;
            }
        }
        
        // Download file
        log_info("File download started");
        while (download_client.connected() || download_client.available()) {
            if (download_client.available()) {
                bytes_read = download_client.readBytes(buffer, sizeof(buffer));
                write(buffer, bytes_read);
            }
        }
        log_info("File download completed");

    } else {
        log_warn("Connection to server failed");
    }

    // Close the connection
    download_client.stop();
}