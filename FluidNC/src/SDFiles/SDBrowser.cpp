// FluidNC/src/SDFiles/SDBrowser.cpp
// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the
// LICENSE file.

#include "src/SDFiles/SDBrowser.h"

#include <cstddef>

namespace sdfiles {

void SDBrowser::open(FileClass cls) {
    _classFilter   = cls;
    _currentDir    = kRootParent;
    _selected      = 0;
    _scrollTop     = 0;
    _selectedEntry = kInvalidEntry;
}

void SDBrowser::resetToRoot() {
    _currentDir    = kRootParent;
    _selected      = 0;
    _scrollTop     = 0;
    _selectedEntry = kInvalidEntry;
}

void SDBrowser::enterDir(EntryId dir) {
    _currentDir    = dir;
    _selected      = 0;
    _scrollTop     = 0;
    _selectedEntry = kInvalidEntry;  // Back row of the new directory
}

bool SDBrowser::isVisible(const SDFileTable& t, EntryId child) const {
    // A directory shows only when it transitively holds a file of the active class; a
    // file shows only when its own class matches.
    return t.isDir(child) ? t.dirContains(child, _classFilter)
                          : (t.fileClass(child) == _classFilter);
}

uint16_t SDBrowser::filteredCount(const SDFileTable& t) const {
    uint16_t n = 0;
    size_t total = t.childCount(_currentDir);
    for (size_t i = 0; i < total; ++i) {
        if (isVisible(t, t.childAt(_currentDir, i))) ++n;
    }
    return n;
}

EntryId SDBrowser::filteredChild(const SDFileTable& t, uint16_t k) const {
    uint16_t seen = 0;
    size_t total = t.childCount(_currentDir);
    for (size_t i = 0; i < total; ++i) {
        EntryId c = t.childAt(_currentDir, i);
        if (!isVisible(t, c)) continue;
        if (seen == k) return c;
        ++seen;
    }
    return kInvalidEntry;
}

uint16_t SDBrowser::rowCount(const SDFileTable& t) const {
    return static_cast<uint16_t>(1 + filteredCount(t));
}

EntryId SDBrowser::entryAtRow(const SDFileTable& t, uint16_t row) const {
    if (row == 0) return kInvalidEntry;
    return filteredChild(t, static_cast<uint16_t>(row - 1));
}

void SDBrowser::moveSelection(const SDFileTable& t, int delta, uint16_t visibleRows) {
    uint16_t rows = rowCount(t);  // always >= 1 (the Back row)
    int sel = static_cast<int>(_selected) + delta;
    if (sel < 0) sel = 0;
    if (sel > rows - 1) sel = rows - 1;
    _selected = static_cast<uint16_t>(sel);
    // Remember what the cursor is on by identity so reconcile() can follow it after
    // a list mutation (kInvalidEntry when parked on the Back row).
    _selectedEntry = entryAtRow(t, _selected);
    if (visibleRows == 0) { _scrollTop = _selected; return; }
    if (_selected < _scrollTop) {
        _scrollTop = _selected;
    } else if (_selected >= _scrollTop + visibleRows) {
        _scrollTop = static_cast<uint16_t>(_selected - visibleRows + 1);
    }
}

uint16_t SDBrowser::rowOfEntry(const SDFileTable& t, EntryId id) const {
    if (id == kInvalidEntry) return kInvalidEntry;
    uint16_t row   = 1;  // row 0 is the Back row
    size_t   total = t.childCount(_currentDir);
    for (size_t i = 0; i < total; ++i) {
        EntryId c = t.childAt(_currentDir, i);
        if (!isVisible(t, c)) continue;
        if (c == id) return row;
        ++row;
    }
    return kInvalidEntry;
}

void SDBrowser::reconcile(const SDFileTable& t, uint16_t visibleRows) {
    // Heal a current directory removed out from under us: climb to the nearest live
    // ancestor, else fall back to root. Bounded by depth in case a tombstone's
    // parent link is unreadable after compaction.
    for (uint16_t guard = 0; _currentDir != kRootParent && guard <= kMaxDepth; ++guard) {
        if (t.isLive(_currentDir)) break;
        _currentDir = t.parent(_currentDir);
    }
    if (_currentDir != kRootParent && !t.isLive(_currentDir)) {
        _currentDir = kRootParent;
    }

    uint16_t rows = rowCount(t);  // always >= 1 (the Back row)

    if (_selectedEntry == kInvalidEntry) {
        // Parked on the Back row (or nothing) — row 0 is always valid.
        if (_selected >= rows) _selected = 0;
    } else {
        uint16_t row = rowOfEntry(t, _selectedEntry);
        if (row != kInvalidEntry) {
            _selected = row;  // follow the selected file to its new row
        } else {
            // The selected file is gone: keep the same row index (the entry that
            // shifted into its place), clamped into range, and re-anchor identity.
            if (_selected > rows - 1) _selected = static_cast<uint16_t>(rows - 1);
            _selectedEntry = entryAtRow(t, _selected);
        }
    }

    // Keep the cursor visible.
    if (visibleRows == 0) {
        _scrollTop = _selected;
        return;
    }
    if (_scrollTop > rows - 1) _scrollTop = 0;
    if (_selected < _scrollTop) {
        _scrollTop = _selected;
    } else if (_selected >= _scrollTop + visibleRows) {
        _scrollTop = static_cast<uint16_t>(_selected - visibleRows + 1);
    }
}

SDBrowser::Activation SDBrowser::activate(const SDFileTable& t, EntryId& outFile) {
    outFile = kInvalidEntry;
    // Act on the entry the user last saw highlighted, by identity, in case the list
    // mutated since the last render. visibleRows = 0: scroll is irrelevant here, and
    // every activation outcome re-renders.
    reconcile(t, 0);
    if (isBackRow(_selected)) {
        if (_currentDir == kRootParent) return Activation::ExitedToMain;
        enterDir(t.parent(_currentDir));
        return Activation::ExitedToParent;
    }
    EntryId target = entryAtRow(t, _selected);
    if (target == kInvalidEntry) return Activation::None;
    if (t.isDir(target)) {
        enterDir(target);
        return Activation::EnteredDir;
    }
    outFile = target;
    return Activation::SelectedFile;
}

}  // namespace sdfiles
