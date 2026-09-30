// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license (see LICENSE).
#include "OledTextFit.h"

// Reference implementation and tests of this mapping:
//   misc-oskay/fluidnc-font/font_table_lookup.py
//   misc-oskay/fluidnc-font/test_font_table_lookup.py
char font_table_lookup(FontLookupState& state, uint8_t ch) {
    uint8_t& prev3 = state.prev3;
    uint8_t& prev2 = state.prev2;
    uint8_t& prev1 = state.prev1;

    // ASCII: passthrough, clear state.
    if (ch < 0x80) {
        prev3 = prev2 = prev1 = 0;
        return (char)ch;
    }

    // Latin-1 passthrough ranges (matches DefaultFontTableLookup).
    if (prev1 == 0xC2) {
        prev3 = prev2 = prev1 = 0;
        return (char)ch;
    }
    if (prev1 == 0xC3) {
        prev3 = prev2 = prev1 = 0;
        return (char)(ch | 0xC0);
    }

    // Mapped 2-byte: ˣ U+02E3 (CB A3) -> 0x82
    if (prev1 == 0xCB && ch == 0xA3) {
        prev3 = prev2 = prev1 = 0;
        return (char)0x82;
    }

    // Mapped 3-byte sequences, lead byte 0xE2.
    if (prev2 == 0xE2 && prev1 == 0x86 && ch == 0x90) {
        prev3 = prev2 = prev1 = 0;
        return (char)0x7F;  // ← U+2190
    }
    if (prev2 == 0xE2 && prev1 == 0x86 && ch == 0x92) {
        prev3 = prev2 = prev1 = 0;
        return (char)0x80;  // → U+2192
    }
    if (prev2 == 0xE2 && prev1 == 0x97 && ch == 0x80) {
        prev3 = prev2 = prev1 = 0;
        return (char)0x81;  // ◀ U+25C0
    }
    if (prev2 == 0xE2 && prev1 == 0x9C && ch == 0x93) {
        prev3 = prev2 = prev1 = 0;
        return (char)0x83;  // ✓ U+2713
    }

    // Mapped 4-byte: 🛜 U+1F6DC (F0 9F 9B 9C) -> 0x84
    if (prev3 == 0xF0 && prev2 == 0x9F && prev1 == 0x9B && ch == 0x9C) {
        prev3 = prev2 = prev1 = 0;
        return (char)0x84;
    }

    // Otherwise: push onto state, return 0 (no glyph yet / drop).
    prev3 = prev2;
    prev2 = prev1;
    prev1 = ch;
    return (char)0;
}

void wrap_to_width(const std::string& s, int max_w, const GlyphWidth& width, std::vector<std::string>& out) {
    if (s.empty()) {
        out.emplace_back();
        return;
    }

    size_t start = 0;
    while (start < s.length()) {
        // Measure glyphs from start until one would overflow the line. fit_end
        // only advances past a byte that completes a glyph, so it always marks
        // a character boundary.
        FontLookupState state;
        int             line_w   = 0;
        size_t          fit_end  = start;
        size_t          space_at = std::string::npos;  // last break point within the line
        size_t          i;
        bool            overflow = false;
        for (i = start; i < s.length(); ++i) {
            char glyph = font_table_lookup(state, static_cast<uint8_t>(s[i]));
            if (glyph == 0) {
                continue;  // inside a multi-byte sequence, or a dropped byte
            }
            const int w = width(static_cast<uint8_t>(glyph));
            if (line_w + w > max_w) {
                if (s[i] == ' ' && i > start) {
                    space_at = i;  // the overflowing space is itself a break point
                }
                overflow = true;
                break;
            }
            line_w += w;
            if (s[i] == ' ' && i > start) {
                space_at = i;
            }
            fit_end = i + 1;
        }

        if (!overflow) {
            out.emplace_back(s.substr(start));
            return;
        }
        if (space_at != std::string::npos) {
            // Break at the most recent space; it is dropped from both lines.
            out.emplace_back(s.substr(start, space_at - start));
            start = space_at + 1;
        } else {
            // No space to break at: break between characters, keeping at least
            // one character on the line even if it alone is too wide.
            const size_t end = (fit_end > start) ? fit_end : i + 1;
            out.emplace_back(s.substr(start, end - start));
            start = end;
        }
    }
}

int text_width(const std::string& s, const GlyphWidth& width) {
    FontLookupState state;
    int             w = 0;
    for (char c : s) {
        char glyph = font_table_lookup(state, static_cast<uint8_t>(c));
        if (glyph != 0) {
            w += width(static_cast<uint8_t>(glyph));
        }
    }
    return w;
}

void cap_lines(std::vector<std::string>& lines, size_t max_lines, int max_w, const GlyphWidth& width) {
    if (max_lines == 0) {
        lines.clear();
        return;
    }
    if (lines.size() <= max_lines) {
        return;
    }
    lines.resize(max_lines);

    std::string& last       = lines.back();
    const int    dots_width = text_width("...", width);
    // Trim whole characters (never a UTF-8 continuation byte on its own)
    // until the line plus the ellipsis fits, then keep the ellipsis attached
    // to the last word rather than a trailing space.
    while (!last.empty() && text_width(last, width) + dots_width > max_w) {
        size_t cut = last.length() - 1;
        while (cut > 0 && (static_cast<uint8_t>(last[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        last.erase(cut);
    }
    while (!last.empty() && last.back() == ' ') {
        last.pop_back();
    }
    last += "...";
}

static size_t line_count(const std::string& s, int max_w, const GlyphWidth& width) {
    std::vector<std::string> lines;
    wrap_to_width(s, max_w, width, lines);
    return lines.size();
}

std::string fit_error_report(const std::string& report, size_t max_lines, int max_w, const GlyphWidth& width) {
    // Anchor on ") in " so an " in " inside the description is not mistaken
    // for the location, and on the last " at line" so a filename containing
    // those words is kept whole.
    const size_t paren_in = report.find(") in ");
    const size_t at_pos   = report.rfind(" at line");
    if (paren_in == std::string::npos || at_pos == std::string::npos || at_pos < paren_in + 5) {
        return report;
    }
    const std::string head     = report.substr(0, paren_in + 1);  // "<code> (<description>)"
    const std::string tail     = report.substr(at_pos);           // " at line <n>"
    const std::string filepath = report.substr(paren_in + 5, at_pos - paren_in - 5);
    const size_t      slash    = filepath.rfind('/');
    const std::string fname    = (slash != std::string::npos) ? filepath.substr(slash + 1) : filepath;

    // wrap_to_width keeps a run intact only when it is narrower than max_w,
    // so a filename that is not would be broken mid-name.
    if (!fname.empty() && text_width(fname, width) < max_w) {
        std::string with_name = head + " in " + fname + tail;
        if (line_count(with_name, max_w, width) <= max_lines) {
            return with_name;
        }
    }
    std::string generic = head + " in G-Code file" + tail;
    if (line_count(generic, max_w, width) <= max_lines) {
        return generic;
    }
    return head + tail;
}
