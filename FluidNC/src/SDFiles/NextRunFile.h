// FluidNC/src/SDFiles/NextRunFile.h
// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the
// LICENSE file.
//
// Pure "what plays next" selection for the end-of-job Run Next feature. Reads the
// arena table's completion marks only; holds no state. The on-disk checkmarks are the
// source of truth, so these answers survive a reboot.

#pragma once

#include "src/SDFiles/SDFileTable.h"

namespace sdfiles {

// First unchecked file in `dir` (in the table's current sorted order), EXCLUDING
// `justRan`. Returns kInvalidEntry when no other unchecked file remains.
EntryId nextRunFile(const SDFileTable& t, EntryId dir, EntryId justRan);

// True when `dir` has no unchecked files at all (the job is done). Counts the real
// current mark state of every file, including justRan.
bool folderComplete(const SDFileTable& t, EntryId dir);

}  // namespace sdfiles
