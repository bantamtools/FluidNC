// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license (see LICENSE).
#pragma once

#include <cstdio>

// Consume a UTF-8 byte-order mark (EF BB BF) at the start of a file that was
// just opened for reading. Returns true if a mark was consumed. Otherwise the
// stream is repositioned to the start and false is returned, so the first read
// sees the file's first byte.
bool skip_utf8_bom(FILE* fd);
