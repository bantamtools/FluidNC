// Copyright (c) 2026 - Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in
// the LICENSE file.

#include "OledDump.h"

#include <cstddef>
#include <cstdint>

namespace oleddump {

size_t bufferSize(uint16_t width, uint16_t height) {
    return static_cast<size_t>(width) * ((height + 7u) / 8u);
}

size_t rowHexLength(uint16_t width) {
    return ((width + 7u) / 8u) * 2u;
}

size_t rowToHex(const uint8_t* buf, uint16_t width, uint16_t height, uint16_t y, char* out, size_t out_size) {
    static const char kDigits[] = "0123456789ABCDEF";
    const size_t      hex_len   = rowHexLength(width);
    if (y >= height || out_size < hex_len + 1) {
        return 0;
    }
    const size_t  page = y / 8u;
    const uint8_t mask = static_cast<uint8_t>(1u << (y & 7u));
    size_t        pos  = 0;
    for (uint16_t byte_x = 0; byte_x < width; byte_x += 8) {
        uint8_t value = 0;
        for (uint16_t bit = 0; bit < 8; ++bit) {
            const uint16_t x = byte_x + bit;
            if (x < width && (buf[x + page * width] & mask)) {
                value |= static_cast<uint8_t>(0x80u >> bit);
            }
        }
        out[pos++] = kDigits[value >> 4];
        out[pos++] = kDigits[value & 0x0F];
    }
    out[pos] = '\0';
    return pos;
}

}  // namespace oleddump
