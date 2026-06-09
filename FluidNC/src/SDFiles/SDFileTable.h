// FluidNC/src/SDFiles/SDFileTable.h
// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the
// LICENSE file.
//
// Fixed-arena in-memory table of SD folder/file entries. See
// docs/plans/2026-05-21-sd-file-menu-arena-design.md.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

namespace sdfiles {

using EntryId = uint16_t;

constexpr EntryId kInvalidEntry = 0xFFFF;  // also means "no parent" (root level)
constexpr EntryId kRootParent   = 0xFFFF;
constexpr size_t  kMaxNameLen   = 255;     // bounded by name_len:u8 (see spec §6)
constexpr size_t  kMaxDepth     = 32;      // max folder nesting fullPath() reconstructs

// Initial production sizing. Final values are set by the WiFi low-water mark
// measurement in a later sizing task; until then these are conservative defaults
// that meet the 300-500 file target with margin (see spec §12).
constexpr size_t   kInitialArenaBytes  = 24 * 1024;  // 24 kB
constexpr uint16_t kInitialMaxEntries  = 800;

enum class FileClass : uint8_t { Gcode = 0, Firmware = 1, Config = 2 };

enum class SortMode : uint8_t { NameAsc = 0, NameDesc = 1, Oldest = 2, Newest = 3 };

// UTF-8 bytes of U+2713 (checkmark) completion prefix.
constexpr char   kCompletionPrefix[] = "\xE2\x9C\x93";
constexpr size_t kCompletionPrefixLen = 3;

class SDFileTable {
public:
    // Allocates the arena (arenaBytes) and handle/index tables (maxEntries) once.
    SDFileTable(size_t arenaBytes, uint16_t maxEntries);

    // Non-copyable (owns the arena buffers). Movable for construction-time use only
    // (e.g. returning a built table by value): the mutex is not moved, so the
    // moved-to table gets a fresh, unlocked one. Moving under concurrent access is
    // not safe; production constructs the single table in place and never moves it.
    SDFileTable(const SDFileTable&)            = delete;
    SDFileTable& operator=(const SDFileTable&) = delete;
    SDFileTable(SDFileTable&&) noexcept;
    SDFileTable& operator=(SDFileTable&&) noexcept;

    // Empties the table without freeing the arena (wholesale reset).
    void reset();

    // INDEX CONTRACT: addEntry/removeEntry/removeSubtree/renameEntry mutate entries but
    // do NOT update the sorted index. Call rebuildIndex() after a batch of mutations
    // (and after a scan refill) before using childCount/childAt/nextIncompleteFile.
    // The re-sort is RAM-only (no disk I/O), cheap relative to the disk catalog.

    // Appends an entry. rawName is the on-disk basename; a leading checkmark prefix is
    // stripped and the completed bit set. Returns the new EntryId, or kInvalidEntry if
    // the arena or handle table is full, or the name (after strip) exceeds kMaxNameLen.
    EntryId addEntry(EntryId parent, FileClass cls, bool isDir, uint32_t mtime,
                     const char* rawName);

    // Tombstones a single entry (clears its live bit). Returns false if id invalid.
    bool removeEntry(EntryId id);

    // Tombstones a directory and all entries beneath it (recursively).
    void removeSubtree(EntryId dir);

    // General rename: tombstones the old entry and appends a new one with newRawName
    // (same parent/class/isDir/mtime). Returns the new EntryId, or kInvalidEntry on
    // failure (in which case the old entry is left live).
    EntryId renameEntry(EntryId id, const char* newRawName);

    // Sets/clears the completed bit (name and sort position unchanged).
    void setCompleted(EntryId id, bool completed);

    // Accessors. Behavior is undefined for kInvalidEntry; callers must pass live ids.
    bool        isDir(EntryId id) const;
    bool        isLive(EntryId id) const;
    bool        isCompleted(EntryId id) const;
    FileClass   fileClass(EntryId id) const;
    uint32_t    mtime(EntryId id) const;
    // Canonical basename, NUL-terminated. The returned pointer is valid only until the
    // next call on this table; to hold several names at once, use copyName().
    const char* name(EntryId id) const;
    // Copies the canonical basename into `out` (NUL-terminated, truncated to cap-1).
    // Safe to hold across other table calls.
    void        copyName(EntryId id, char* out, size_t cap) const;
    EntryId     parent(EntryId id) const;

    // Rebuilds the index over live entries: grouped by parent, then ordered by mode.
    void rebuildIndex(SortMode mode);

    // Sorted-order folder listing (uses the current index).
    size_t  childCount(EntryId dir) const;
    EntryId childAt(EntryId dir, size_t i) const;  // kInvalidEntry if out of range
    // True iff `dir` is a valid live directory that transitively contains at least one
    // file of class `cls`. False for a non-directory, an invalid id, or a directory
    // with no content of that class. Reflects the content mask as of the last
    // rebuildIndex().
    bool    dirContains(EntryId dir, FileClass cls) const;
    // First live child of `dir` whose canonical name equals `name` (checkmark-stripped,
    // as stored), or kInvalidEntry. Scans entries directly, NOT the index, so it is
    // valid before rebuildIndex (used for path resolution during mutations).
    EntryId findChild(EntryId dir, const char* name) const;
    // Completion-aware overload: like findChild(dir, name) but additionally requires
    // the entry's completed bit to equal `completed`. When both a completed and an
    // incomplete entry share the same canonical name under the same parent, this
    // overload selects the correct variant. Returns kInvalidEntry if the requested
    // completion variant is absent.
    EntryId findChild(EntryId dir, const char* name, bool completed) const;

    // Reconstructs the base-relative path ("/folder/sub/basename"; leading '/', no
    // "/sd") into `out`. includeCompletionMark=true re-inserts the checkmark prefix on
    // the basename when the entry's completed bit is set, giving the exact on-disk name.
    // Allocation-free; returns false (out left empty) if it does not fit or the tree is
    // deeper than kMaxDepth.
    bool fullPath(EntryId id, char* out, size_t cap,
                  bool includeCompletionMark = false) const;

    // Next incomplete file in dir after `after` in sorted order; pass kInvalidEntry for
    // `after` to get the first. Skips directories and completed files. kInvalidEntry
    // when none remain.
    EntryId nextIncompleteFile(EntryId dir, EntryId after) const;

    // True when a further addEntry of a minimal entry could not fit (arena or handles).
    bool full() const;

    size_t liveCount() const;  // the "N" in "showing N"

    // Current sort mode (the mode passed to the most recent rebuildIndex()).
    SortMode sortMode() const { return _sortMode; }

    // Scan completeness: false once a scan was truncated by the arena cap or the
    // file-count ceiling. Defaults true (an empty/never-scanned table truncated
    // nothing). Distinct from the per-entry isCompleted() completion API.
    bool scanComplete() const { return _scanComplete; }
    void setScanComplete(bool complete) { _scanComplete = complete; }

    // Sorted-index access (valid after rebuildIndex). indexAt(i) is the i-th
    // EntryId in sorted order; used to walk all live entries in one pass.
    size_t  indexCount() const { return _indexCount; }
    EntryId indexAt(size_t i) const { return _index[i]; }

    // Reclaims arena bytes occupied by tombstoned entries; preserves live EntryIds.
    void compact();

    // CONCURRENCY: the arena (entries, _entryOffset, _index, content-mask bits) is
    // read and written from BOTH the poller task (core 0: the OLED scroll-render, and
    // WebUI/serial/card-detect mutations) and the protocol task (core 1: nav render,
    // completion marking, the  re-sort). The methods above take NO lock. Callers
    // MUST hold this mutex across each whole critical section — a multi-step read from
    // id-resolution through value copy-out, or a mutation through its rebuildIndex() —
    // so the other core never observes a half-rebuilt index or a mid-compact
    // _entryOffset. Recursive because render paths re-enter: a write site's trailing
    // refresh_display()/clear_popup() reaches show_menu(), which locks for its own read.
    //
    // DEADLOCK INVARIANT: never acquire this mutex inside a vTaskSuspend(pollingTask)
    // window. The suspend is async and can freeze the poller while it holds this lock
    // (mid-render); a protocol-task path that suspended the poller and then took this
    // lock would block forever. The existing suspend sites touch only the OLED buffer
    // and the legacy ListType menu, not the arena — keep it that way.
    std::recursive_mutex& mutex() const { return _mutex; }

private:
    struct Header;  // defined in the .cpp

    Header*       headerAt(EntryId id);
    const Header* headerAt(EntryId id) const;
    static const char* stripCompletion(const char* rawName, bool& completedOut);
    int compareNames(EntryId a, EntryId b) const;  // length-aware, copy-free

    std::unique_ptr<uint8_t[]>  _arena;
    std::unique_ptr<uint32_t[]> _entryOffset;  // EntryId -> arena byte offset
    std::unique_ptr<EntryId[]>  _index;
    size_t   _arenaCap;
    size_t   _arenaUsed;
    uint16_t _maxEntries;
    uint16_t _entryCount;  // next EntryId to assign
    uint16_t _indexCount;
    SortMode _sortMode;
    bool     _scanComplete = true;

    // Guards all cross-task arena access; see mutex() above. Mutable so the render
    // path can lock through a const SDFileTable&.
    mutable std::recursive_mutex _mutex;
};

}  // namespace sdfiles
