// FluidNC/src/SDFiles/NextRunFile.cpp
// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the
// LICENSE file.

#include "src/SDFiles/NextRunFile.h"

namespace sdfiles {

namespace {
// First incomplete GCODE file in `dir` after `after`, skipping non-gcode entries
// (.yaml config, .bin firmware). nextIncompleteFile() skips directories and completed
// files but not file class, so the Plot Next selection must filter to gcode itself —
// otherwise it could "plot" a config or firmware file (which never get a completion
// mark, so they always read as incomplete).
EntryId nextIncompleteGcode(const SDFileTable& t, EntryId dir, EntryId after) {
    EntryId c = t.nextIncompleteFile(dir, after);
    while (c != kInvalidEntry && t.fileClass(c) != FileClass::Gcode) {
        c = t.nextIncompleteFile(dir, c);
    }
    return c;
}
}  // namespace

EntryId nextRunFile(const SDFileTable& t, EntryId dir, EntryId justRan) {
    EntryId c = nextIncompleteGcode(t, dir, kInvalidEntry);
    if (c == justRan) {
        c = nextIncompleteGcode(t, dir, justRan);
    }
    return c;
}

bool folderComplete(const SDFileTable& t, EntryId dir) {
    return nextIncompleteGcode(t, dir, kInvalidEntry) == kInvalidEntry;
}

}  // namespace sdfiles
