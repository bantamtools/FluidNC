// Copyright (c) 2023 -  Matt Staniszewski
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
 * USB Serial driver that accesses the ESP32 hardware via the Arduino Serial interface
 */

#include "Usb.h"
#include <Arduino.h>


Usb::Usb() : _usb_num(0) {}

void Usb::begin() {

    Serial.begin(115200);
}

int Usb::read() {
    if (_pushback != -1) {
        int ret   = _pushback;
        _pushback = -1;
        return ret;
    }
    return Serial.read();
}

size_t Usb::write(uint8_t c) {
    return Serial.write(c);
}

size_t Usb::write(const uint8_t* buffer, size_t length) {
    return Serial.write(buffer, length);
}

size_t Usb::timedReadBytes(char* buffer, size_t len, TickType_t timeout) {

    Serial.setTimeout(timeout);
    int res = Serial.read(buffer, len);
    // If res < 0, no bytes were read

    return res < 0 ? 0 : res;
}

void Usb::config_message(const char* prefix, const char* usage) {
    log_info(prefix << usage);
}

int Usb::rx_buffer_available(void) {
    return 256 - available();  // Based on RX/TX FIFO sizes in HWCDC
}

int Usb::peek() {
    if (_pushback != -1) {
        return _pushback;
    }
    int ch = Serial.read();
    if (ch == -1) {
        return -1;
    }
    _pushback = ch;
    return ch;
}

int Usb::available() {
    return Serial.available() + (_pushback != -1);
}

void Usb::flushRx() {
    _pushback = -1;
    // Don't use Serial.flush(), which clears TX instead of RX
    while(Serial.available()) {
        Serial.read();
    }
}
