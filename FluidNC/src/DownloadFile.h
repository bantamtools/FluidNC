// Copyright (c) 2023 -	Matt Staniszewski, Bantam Tools

#pragma once

#include "WebUI/Authentication.h"
#include "FileStream.h"  // FileStream and Channel
#include "Error.h"

#include <cstdint>

class DownloadFile : public FileStream {
private:

    int _total_bytes_read;
    int _size;

    // The channel that triggered the use of this file, through which
    // status about the use of this file will be reported.
    Channel& _out;

public:
    static std::string _progress;

    // path is the full path to the file
    // channel is the I/O channel on which status about the use of this file will be reported
    DownloadFile(const char* path, int size, Channel& out);

    DownloadFile(const DownloadFile&) = delete;
    DownloadFile& operator=(const DownloadFile&) = delete;

    // This is used for feedback about the progress of the operation
    float    percent_complete();

    // This tells where to send the feedback
    Channel& getChannel() { return _out; }

    // Channel methods
    size_t   write(const uint8_t* buffer, size_t length) override;

    ~DownloadFile();
};
