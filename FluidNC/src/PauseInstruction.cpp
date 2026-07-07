// Copyright (c) 2026 - Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in
// the LICENSE file.

#include "PauseInstruction.h"

#include <cstring>

namespace {

// Number of bytes in the UTF-8 sequence that STARTS at lead byte b. Defensive:
// stray continuation bytes / invalid leads count as 1 so we never advance past
// the terminating NUL or misread malformed input.
size_t utf8_seq_len(unsigned char b) {
    if (b < 0x80) return 1;
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return 1;
}

// Bytes we strip from the payload: markup, ampersand, newline/CR, and C0/DEL
// control chars. Only applied at single-byte (ASCII) positions; every stripped
// byte is < 0x80, so multibyte lead/continuation bytes always pass through.
bool is_stripped(unsigned char c) {
    return c == ']' || c == '<' || c == '>' || c == '&' || c < 0x20 || c == 0x7F;
}

}  // namespace

void sanitize_pause_instruction(const char* in, char* out) {
    size_t o         = 0;
    bool   truncated = false;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(in ? in : "");
    while (*p) {
        unsigned char c   = *p;
        size_t        seq = utf8_seq_len(c);
        if (seq == 1 && is_stripped(c)) {
            ++p;
            continue;
        }
        if (o + seq > PAUSE_INSTR_MAX) {  // whole codepoint would overflow the cap
            truncated = true;
            break;
        }
        for (size_t i = 0; i < seq && *p; ++i) {
            out[o++] = static_cast<char>(*p++);
        }
    }
    if (truncated) {
        // Roll back whole codepoints until the 3-byte "..." fits within the cap.
        while (o + 3 > PAUSE_INSTR_MAX && o > 0) {
            do {
                --o;
            } while (o > 0 && (static_cast<unsigned char>(out[o]) & 0xC0) == 0x80);
        }
        out[o++] = '.';
        out[o++] = '.';
        out[o++] = '.';
    }
    out[o] = '\0';
}

void pause_instruction_set(char* cache, const char* text) {
    sanitize_pause_instruction(text, cache);
}

bool pause_instruction_active(const char* cache) {
    return cache[0] != '\0';
}

bool pause_instruction_clear(char* cache) {
    if (cache[0] == '\0') {
        return false;
    }
    cache[0] = '\0';
    return true;
}

void format_instr_line(const char* text, std::string& out) {
    out  = PAUSE_INSTR_SET_PREFIX;  // "[MSG:INSTR:"
    out += text;
    out += ']';
}
