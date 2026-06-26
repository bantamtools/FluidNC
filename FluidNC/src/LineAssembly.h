// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license (see LICENSE).
#pragma once

#include "Error.h"

// How a line read by assemble_line() ended. EofWithContent is the truncation
// signal: the file ended mid-line, with no terminating newline.
enum class LineTerm { Newline, EofWithContent, EofEmpty, TooLong };

// Assemble one line by pulling bytes from nextByte(ctx): a value >= 0 is a byte,
// a value < 0 means end of input. Strips '\r', stops at '\n' or end of input,
// writes into line (null-terminated) and sets out_len. The maxlen guard mirrors
// the historical readLine() behavior: it triggers TooLong before storing the
// overflowing byte and does not null-terminate on that path.
LineTerm assemble_line(char* line, int maxlen, int& out_len, int (*nextByte)(void* ctx), void* ctx);

// Map a LineTerm onto the Error reported by InputFile::readLine(), updating
// ended_midline ONLY for content lines. EofEmpty (the trailing empty read after
// a truncated line) returns Error::Eof and leaves ended_midline untouched, so the
// truncation bit set by the preceding content line survives into the EOF handler.
Error term_to_error(LineTerm term, bool& ended_midline);
