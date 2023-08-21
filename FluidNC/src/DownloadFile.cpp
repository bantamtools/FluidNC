// Copyright (c) 2023 -	Matt Staniszewski, Bantam Tools

#include "DownloadFile.h"

#include "Report.h"

DownloadFile::DownloadFile(const char* path, int size, Channel& out) :
    FileStream(path, "w", "sd"), _total_bytes_read(0), _size(size), _out(out) {
    setReportInterval(250);
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
   
    return (float)_total_bytes_read / (float)_size * 100.0f;
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
