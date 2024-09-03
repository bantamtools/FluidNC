// Copyright (c) 2023 -  Matt Staniszewski
// Copyright (c) 2023 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.
#include "UsbChannel.h"
#include "Machine/MachineConfig.h"  // config
#include "Serial.h"                 // allChannels

// Define the Usb0 object
UsbChannel Usb0(false);  // Set to true if you want CRLF conversion enabled

UsbChannel::UsbChannel(bool addCR) : Channel("usb_channel", addCR) {
    _lineedit = new Lineedit(this, _line, Channel::maxLine - 1);
    _active   = false;
}

void UsbChannel::init(Usb* usb) {
    _usb = usb;
    allChannels.registration(this);
    _active = true;
    log_info("usb_channel initialized");
    log_msg_to(*this, "RST");
}

size_t UsbChannel::write(uint8_t c) {
    return _usb->write(c);
}

size_t UsbChannel::write(const uint8_t* buffer, size_t length) {
    if (_addCR) {
        size_t rem      = length;
        char   lastchar = '\0';
        size_t j        = 0;
        while (rem) {
            const int bufsize = 80;
            uint8_t   modbuf[bufsize];
            size_t k = 0;
            while (rem && k < (bufsize - 1)) {
                char c = buffer[j++];
                if (c == '\n' && lastchar != '\r') {
                    modbuf[k++] = '\r';
                }
                lastchar    = c;
                modbuf[k++] = c;
                --rem;
            }
            _usb->write(modbuf, k);
        }
        return length;
    } else {
        return _usb->write(buffer, length);
    }
}

int UsbChannel::available() {
    return _usb->available();
}

int UsbChannel::peek() {
    return _usb->peek();
}

int UsbChannel::read() {
    return _usb->read();
}

void UsbChannel::flushRx() {
    _usb->flushRx();
    Channel::flushRx();
}

size_t UsbChannel::timedReadBytes(char* buffer, size_t length, TickType_t timeout) {
    size_t remlen = length;
    while (remlen && _queue.size()) {
        *buffer++ = _queue.front();
        _queue.pop();
    }

    int res = _usb->timedReadBytes(buffer, remlen, timeout);
    remlen -= (res < 0) ? 0 : res;
    return length - remlen;
}

bool UsbChannel::realtimeOkay(char c) {
    return _lineedit->realtime(c);
}

bool UsbChannel::lineComplete(char* line, char c) {
    if (_lineedit->step(c)) {
        _linelen        = _lineedit->finish();
        _line[_linelen] = '\0';
        strcpy(line, _line);
        _linelen = 0;
        return true;
    }
    return false;
}

Error UsbChannel::pollLine(char* line) {
    if (_lineedit == nullptr) {
        return Error::NoData;
    }
    return Channel::pollLine(line); 
}

int UsbChannel::rx_buffer_available() {
    return _usb->rx_buffer_available();
}

void usbInit() {
    auto usb0 = new Usb();
    usb0->begin();
    Usb0.init(usb0);
    log_info("usb_channel initialized");
}


