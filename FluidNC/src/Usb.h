// Copyright (c) 2023 -  Matt Staniszewski
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Config.h"

#include "Configuration/Configurable.h"
#include "Configuration/GenericFactory.h"

#include <freertos/FreeRTOS.h>  // TickType_T

class Usb : public Stream, public Configuration::Configurable {
private:
    // One character of pushback for implementing peek().
    // We cannot use the queue for this because the queue
    // is after the check for realtime characters, whereas
    // peek() deals with characters before realtime ones
    // are handled.
    int _pushback = -1;
    
    int _usb_num  = 0;  // Hardware USB engine number

public:

    // Name is required for the configuration factory to work.
    const char* name() {
        static char nstr[5] = "usbN";
        nstr[3]             = _usb_num - '0';
        return nstr;
    }

    Usb();
    void begin();

    // Stream methods - Uart must inherit from Stream because the TMCStepper library
    // needs a Stream instance.
    int peek(void) override;
    int available(void) override;
    int read(void) override;

    // Print methods (Stream inherits from Print)
    size_t write(uint8_t data) override;
    size_t write(const uint8_t* buffer, size_t length) override;

    // Support methods for UsbChannel
    void   flushRx();
    int    rx_buffer_available(void);
    size_t timedReadBytes(char* buffer, size_t len, TickType_t timeout);
    size_t timedReadBytes(uint8_t* buffer, size_t len, TickType_t timeout) { return timedReadBytes((char*)buffer, len, timeout); }

    void afterParse() override {}

    void group(Configuration::HandlerBase& handler) override {}

    void config_message(const char* prefix, const char* usage);

};

using UsbFactory = Configuration::GenericFactory<Usb>;
