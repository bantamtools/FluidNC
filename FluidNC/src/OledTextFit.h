// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license (see LICENSE).
#pragma once

// Text layout for the OLED, kept free of display and font machinery so it
// can be unit-tested. Strings are UTF-8; they are measured one glyph at a
// time through font_table_lookup, the same mapping the display applies when
// drawing. Glyph widths come from the caller through a GlyphWidth function,
// which takes a font glyph code the way OLED::char_width does.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

using GlyphWidth = std::function<int(uint8_t)>;

// Bytes of a multi-byte sequence seen so far, most recent in prev1.
struct FontLookupState {
    uint8_t prev3 = 0;
    uint8_t prev2 = 0;
    uint8_t prev1 = 0;
};

// Maps the next byte of a UTF-8 string to a glyph code in the OLED font.
// ASCII passes through; Latin-1 (C2/C3 lead bytes) maps to its code point;
// ←, →, ◀, ˣ, ✓ and 🛜 map to the font's custom slots 0x7F-0x84. Returns 0
// for a byte inside a multi-byte sequence and for characters the font lacks.
char font_table_lookup(FontLookupState& state, uint8_t ch);

// Word-wraps s into lines no wider than max_w pixels, appending them to out.
// Splits at the most recent space when possible (the space itself is
// dropped); a run without spaces breaks between characters, never inside a
// multi-byte one. Every line holds at least one character.
void wrap_to_width(const std::string& s, int max_w, const GlyphWidth& width, std::vector<std::string>& out);

// Total advance width of s in pixels.
int text_width(const std::string& s, const GlyphWidth& width);

// Limits lines to at most max_lines. When lines are dropped, the last kept
// line ends in "..." (trimmed as needed so it still fits within max_w) to
// show the text continues.
void cap_lines(std::vector<std::string>& lines, size_t max_lines, int max_w, const GlyphWidth& width);

// Shortens an error report of the form
//   "<code> (<description>) in <path> at line <n>"
// so it wraps into at most max_lines lines of max_w pixels. The location is
// the most specific of these that fits:
//   "... in <filename> at line <n>"   (directory stripped; the filename must
//                                      also fit on one line by itself)
//   "... in G-Code file at line <n>"
//   "... at line <n>"
// Reports in any other form are returned unchanged.
std::string fit_error_report(const std::string& report, size_t max_lines, int max_w, const GlyphWidth& width);
