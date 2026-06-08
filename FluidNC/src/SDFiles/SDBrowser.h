// FluidNC/src/SDFiles/SDBrowser.h
// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the
// LICENSE file.
//
// Navigation model for browsing one class-view (gcode/firmware/config) of the
// SDFileTable arena on the OLED. Concrete and allocation-free: a few scalars plus
// queries against an existing SDFileTable. No Arduino/ESP dependencies; host-testable.
// The table's sorted index must be current (rebuildIndex) before browsing.

#pragma once

#include "src/SDFiles/SDFileTable.h"

#include <cstdint>

namespace sdfiles {

class SDBrowser {
public:
    enum class Activation { None, EnteredDir, ExitedToParent, ExitedToMain, SelectedFile };

    void open(FileClass cls);

    // Returns the browser to the root directory and clears selection and scroll,
    // preserving the class filter. Use after a full rescan, which reassigns
    // EntryIds and would leave currentDir stale.
    void resetToRoot();

    FileClass classFilter() const { return _classFilter; }
    EntryId   currentDir()  const { return _currentDir; }
    uint16_t  selected()    const { return _selected; }
    uint16_t  scrollTop()   const { return _scrollTop; }

    static bool isBackRow(uint16_t row) { return row == 0; }

    uint16_t rowCount(const SDFileTable& t) const;
    EntryId  entryAtRow(const SDFileTable& t, uint16_t row) const;
    void     moveSelection(const SDFileTable& t, int delta, uint16_t visibleRows);
    Activation activate(const SDFileTable& t, EntryId& outFile);

    // Re-anchor the cursor against the current table after the list may have been
    // mutated from another task (upload/delete/rename/card events) while browsing.
    // Selection is tracked by entry IDENTITY, not row: the cursor follows its file
    // to its new row; if that file is gone, it keeps the same row index (the entry
    // that shifted into its place). Also heals a current directory removed out from
    // under us by climbing to the nearest live ancestor (else root), and re-clamps
    // scroll to keep the cursor visible. Call before reading selection for render.
    void reconcile(const SDFileTable& t, uint16_t visibleRows);

private:
    bool     isVisible(const SDFileTable& t, EntryId child) const;
    uint16_t filteredCount(const SDFileTable& t) const;
    EntryId  filteredChild(const SDFileTable& t, uint16_t k) const;
    // Row of `id` among the current dir's visible children (1-based; row 0 is Back),
    // or kInvalidEntry if `id` is not a visible child here.
    uint16_t rowOfEntry(const SDFileTable& t, EntryId id) const;
    void     enterDir(EntryId dir);

    EntryId   _currentDir    = kRootParent;
    uint16_t  _selected      = 0;
    uint16_t  _scrollTop     = 0;
    EntryId   _selectedEntry = kInvalidEntry;  // identity of the selected row; kInvalidEntry = Back row
    FileClass _classFilter   = FileClass::Gcode;
};

}  // namespace sdfiles
