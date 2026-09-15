// Copyright (c) 2026 - Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in
// the LICENSE file.
//
// Framebuffer-to-text conversion for the $OLED/Dump command. Pure and
// host-testable: no Arduino or display-library dependencies.
//
// The display library stores pixels page-major: byte buf[x + (y / 8) * width],
// bit (y & 7). A dump row is row-major hex: one line per pixel row, with pixel
// x = 0 in the most significant bit of the first byte, so the hex reads left to
// right like the screen.

#pragma once

#include <cstddef>
#include <cstdint>

namespace oleddump {

// Hex characters in one row at the largest supported width (128).
constexpr size_t kMaxRowHex = 32;

// Bytes in a page-major framebuffer of the given geometry.
size_t bufferSize(uint16_t width, uint16_t height);

// Hex characters in one dump row for the given width (two per byte).
size_t rowHexLength(uint16_t width);

// Writes pixel row y of the page-major buffer `buf` as uppercase hex into `out`,
// NUL-terminated. Returns the number of hex characters written, or 0 if y is not
// a row of the buffer or `out_size` is less than rowHexLength(width) + 1.
size_t rowToHex(const uint8_t* buf, uint16_t width, uint16_t height, uint16_t y, char* out, size_t out_size);

}  // namespace oleddump
