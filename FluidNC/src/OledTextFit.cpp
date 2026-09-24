// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license (see LICENSE).
#include "OledTextFit.h"

void wrap_to_width(const std::string& s, int max_w, const GlyphWidth& width, std::vector<std::string>& out) {
    if (s.empty()) {
        out.emplace_back();
        return;
    }

    size_t slen      = s.length();
    size_t swidth    = 0;
    size_t i;
    size_t lastSpace = 0;
    for (i = 0; i < slen && swidth < (size_t)max_w; i++) {
        swidth += width(static_cast<uint8_t>(s[i]));
        if (s[i] == ' ') {
            lastSpace = i;
        }
        if (swidth > (size_t)max_w) {
            break;
        }
    }
    if (swidth < (size_t)max_w) {
        out.emplace_back(s);
        return;
    }
    if (lastSpace == 0) {
        // No spaces found in this width; break at character.
        out.emplace_back(s.substr(0, i));
        wrap_to_width(s.substr(i, slen), max_w, width, out);
    } else {
        // Break at most recent space (skip the space itself on the
        // continuation line).
        out.emplace_back(s.substr(0, lastSpace));
        wrap_to_width(s.substr(lastSpace + 1, slen), max_w, width, out);
    }
}

int text_width(const std::string& s, const GlyphWidth& width) {
    int w = 0;
    for (char c : s) {
        w += width(static_cast<uint8_t>(c));
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
    std::string generic = head + " in G-code file" + tail;
    if (line_count(generic, max_w, width) <= max_lines) {
        return generic;
    }
    return head + tail;
}
