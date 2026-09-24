// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license (see LICENSE).
#pragma once

// Text layout for the OLED, kept free of display and font machinery so it
// can be unit-tested. Glyph widths come from the caller through a GlyphWidth
// function, which measures one byte at a time the way OLED::char_width does.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

using GlyphWidth = std::function<int(uint8_t)>;

// Word-wraps s into lines no wider than max_w pixels, appending them to out.
// Splits at the most recent space when possible (the space itself is
// dropped); falls back to a mid-character break for a run without spaces.
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
//   "... in G-code file at line <n>"
//   "... at line <n>"
// Reports in any other form are returned unchanged.
std::string fit_error_report(const std::string& report, size_t max_lines, int max_w, const GlyphWidth& width);
