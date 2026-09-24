// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license (see LICENSE).
#include "Utf8Bom.h"

bool skip_utf8_bom(FILE* fd) {
    static const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    unsigned char              head[3];
    if (fread(head, 1, 3, fd) == 3 && head[0] == bom[0] && head[1] == bom[1] && head[2] == bom[2]) {
        return true;
    }
    clearerr(fd);  // a file shorter than three bytes sets EOF; clear it before rewinding
    fseek(fd, 0, SEEK_SET);
    return false;
}
