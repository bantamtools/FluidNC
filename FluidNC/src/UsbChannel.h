// Copyright (c) 2023 -  Matt Staniszewski
// Copyright (c) 2023 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Usb.h"
#include "Channel.h"
#include "lineedit.h"

class UsbChannel : public Channel, public Configuration::Configurable {
private:
    Lineedit* _lineedit;
    Usb*      _usb;
    int       _usb_num = 0;

public:
    UsbChannel(bool addCR = false);

    void init(Usb* usb);

    // Print methods (Stream inherits from Print)
    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buf, size_t len) override;

    // Stream methods (Channel inherits from Stream)
    int peek(void) override;
    int available(void) override;
    int read() override;

    // Channel methods
    int rx_buffer_available() override;
    void flushRx() override;
    size_t timedReadBytes(char* buffer, size_t length, TickType_t timeout);
    size_t timedReadBytes(uint8_t* buffer, size_t length, TickType_t timeout) { return timedReadBytes((char*)buffer, length, timeout); }
    bool realtimeOkay(char c) override;
    bool lineComplete(char* line, char c) override;
    Error pollLine(char* line) override;

    // Configuration methods
    void group(Configuration::HandlerBase& handler) override { handler.item("usb_num", _usb_num); }
};

// Extern declaration for the global UsbChannel object
extern UsbChannel Usb0;

extern void usbInit();