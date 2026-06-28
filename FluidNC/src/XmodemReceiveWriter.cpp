// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.
#include "XmodemReceiveWriter.h"

#include <cstring>

bool XmodemReceiveWriter::writeAll(const uint8_t* buf, size_t len) {
    if (len == 0) {
        return true;
    }
    size_t written = _sink(buf, len);
    if (written != len) {
        return false;  // short write — SD full / write error
    }
    _total += written;
    return true;
}

bool XmodemReceiveWriter::acceptPacket(const uint8_t* buf, size_t len) {
    if (_heldLen > 0) {
        if (!writeAll(_held, _heldLen)) {
            return false;
        }
        _heldLen = 0;
    }
    if (len > MAX_PACKET) {
        len = MAX_PACKET;  // defensive: never overrun _held (bufsz is 128/1024)
    }
    std::memcpy(_held, buf, len);
    _heldLen = len;
    return true;
}

bool XmodemReceiveWriter::flushFinal() {
    if (_heldLen == 0) {
        return true;
    }
    size_t count = _heldLen;
    while (count > 0 && _held[count - 1] == CTRLZ) {
        --count;
    }
    bool ok  = writeAll(_held, count);
    _heldLen = 0;
    return ok;
}
