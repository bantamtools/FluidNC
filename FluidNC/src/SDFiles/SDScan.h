// FluidNC/src/SDFiles/SDScan.h
// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the
// LICENSE file.
//
// Per-entry helper that the existing SD scan calls inside its iterator loop to
// populate an SDFileTable alongside the legacy List menu. The scan owns the
// iterator and the per-extension routing; this helper just resolves the parent
// EntryId and appends. Pure C++; no Arduino/ESP dependencies; host-testable.

#pragma once

#include "src/SDFiles/SDFileTable.h"

#include <cstdint>

namespace sdfiles {
namespace SDScan {

// Adds one entry (dir or file) to the arena. The parent EntryId is located by
// resolving the path's prefix via SDMenu::resolvePath. Callers MUST add entries
// in topological order (parent dirs before their children);
// std::filesystem::recursive_directory_iterator yields entries in that order.
//
// Returns false on: arena/handle-table full, a malformed path (missing leading
// '/', no basename), or an unresolvable parent prefix (defensive — caller's
// iterator ordering would be broken).
//
// For directory entries, the `cls` argument is don't-care; pass any FileClass.
// Folders are added once and shared across the three logical menus; files carry
// their FileClass and are filtered by the OLED's per-menu render.
bool addScannedEntry(SDFileTable& t, const char* relPath, bool isDir,
                     uint32_t mtime, FileClass cls);

// Event-driven (incremental) arena mutations. Unlike addScannedEntry — which the
// full scan calls in topological order after reset() — these serve the single-file
// upload/delete path, where parent directories may not yet exist and an entry may
// already be present.

// Upsert a file/dir at relPath. Missing parent directories along the prefix are
// created (the scan adds dirs before files; the event path has no such guarantee).
// If a live entry already exists at relPath it is removed first (idempotent
// overwrite). On a full arena the table is compacted once and the append retried.
// Returns false on a null/malformed path or if the arena stays full after compaction.
//
// The idempotent removal is a single-entry tombstone (removeEntry), so this is for
// files and leaf/new directories. If relPath names an existing directory that still
// has live children, remove it via removeSubtreeByPath first — replacing it here
// would tombstone only the directory and orphan its children.
bool addOrReplaceEntry(SDFileTable& t, const char* relPath, bool isDir,
                       uint32_t mtime, FileClass cls);

// Remove the single live entry at relPath. Returns true if one was found and
// removed; false on a null/malformed path or no matching live entry.
bool removeEntryByPath(SDFileTable& t, const char* relPath);

// Remove the directory entry at relPath together with its whole subtree. Returns
// true if the directory was found and removed; false on null/malformed path or no
// match.
bool removeSubtreeByPath(SDFileTable& t, const char* relPath);

// Applies a completion mark/unmark to the arena after the on-disk rename.
//   sourceRel : the entry being transitioned, by its PRE-transition path
//               (mark: "/init.gcode" [incomplete]; unmark: "/✓init.gcode" [completed]).
//   destRel   : the post-transition path; its leaf ✓-prefix gives the new completed state
//               (mark: "/✓init.gcode" -> completed; unmark: "/init.gcode" -> incomplete).
// Returns true iff an entry was removed (the index changed and the caller should rebuild).
// Twin resolution is unconditional: the arena determines whether a same-canonical twin
// must be collapsed; disk state is not consulted.
bool applyCompletionTransition(SDFileTable& t, const char* sourceRel,
                               const char* destRel);

// Returns true if relPath names a hidden file: either the first path component begins with
// '.' (i.e. the path starts with "/.") or the leaf basename (the part after the last '/')
// begins with '.'. A hidden middle path component does not make the path hidden.
// Returns false for a null pointer.
bool isHiddenPath(const char* relPath);

}  // namespace SDScan
}  // namespace sdfiles
