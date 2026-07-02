// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license (see LICENSE).
#include "LineAssembly.h"

LineTerm assemble_line(char* line, int maxlen, int& out_len, int (*nextByte)(void* ctx), void* ctx) {
    int len = 0;
    int c   = -1;
    while ((c = nextByte(ctx)) >= 0) {
        if (len >= maxlen) {
            out_len = len;
            return LineTerm::TooLong;  // matches readLine(): no store, no null-terminate
        }
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            break;
        }
        line[len++] = c;
    }
    line[len] = '\0';
    out_len   = len;
    if (c == '\n') {
        return LineTerm::Newline;
    }
    return len > 0 ? LineTerm::EofWithContent : LineTerm::EofEmpty;
}

Error term_to_error(LineTerm term, bool& ended_midline) {
    switch (term) {
        case LineTerm::TooLong:
            return Error::LineLengthExceeded;
        case LineTerm::EofEmpty:
            return Error::Eof;  // bit untouched: preserves the prior line's signal
        case LineTerm::Newline:
            ended_midline = false;
            return Error::Ok;
        case LineTerm::EofWithContent:
            // A truncated final line: detected here and routed to EOF so it is
            // not executed. The bit tells the EOF handler the file ended mid-line.
            ended_midline = true;
            return Error::Eof;
    }
    return Error::Ok;  // unreachable; silences -Werror=return-type on GCC
}
