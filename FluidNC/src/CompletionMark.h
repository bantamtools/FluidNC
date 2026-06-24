// CompletionMark.h
//
// Tracks plot completion state on the SD card by prefixing successfully-
// completed filenames with U+2713 (✓). The prefix is the on-disk source
// of truth — no sidecar database. A single Setting (completion_marking)
// gates both adding the prefix on success and stripping it on re-run.
//
// Design spec: docs/plans/2026-04-27-completed-file-checkmark-design.md
// Plan:        docs/plans/2026-04-27-completed-file-checkmark-plan.md
// Issue:       internal tracker
//
// Public seam: has_completion_prefix() is the predicate that future
// Plot Next File logic  should consume to detect completed
// files. Do not build a parallel detection mechanism.

#pragma once

#include <cstddef>
#include <string>

#include "Error.h"

namespace CompletionMark {

// UTF-8 encoding of U+2713 (✓), the completion sentinel byte sequence.
// Exact byte match — not Unicode-equivalence, not "check-like" glyphs.
// The prefix is a sentinel byte sequence the firmware writes and reads;
// treating other glyphs as equivalent invites a UTF-8 normalization
// rabbit hole and breaks the round-trip guarantee that what we wrote
// is what we read.
inline constexpr char   kPrefix[]  = "\xE2\x9C\x93";
inline constexpr size_t kPrefixLen = 3;

// Public seam for /. Returns true iff `basename` starts with
// the exact 3-byte sentinel. No I/O. Safe to call on null or empty
// input.
bool has_completion_prefix(const char* basename);

// Pure path transforms. Operate on the basename of the path; directory
// portion is unchanged. Empty result on strip (input is "/dir/✓") yields
// an empty std::string — caller must check before using.
std::string compute_marked_path(const char* path);
std::string compute_unmarked_path(const char* path);

// On-disk rename. Returns the rename layer's Error code on failure.
// Clobbers an existing destination by removing it first, then renaming the
// source into place — no temp file. The source never moves until the
// destination is gone, so an interruption never strands it.
//
// Error::Ok                 rename committed (or already in target state)
// Error::InvalidValue       path is null/empty, or strip would leave
//                           empty basename
// Error::FsFailedRenameFile any underlying rename failure
Error mark_completed(const char* path);
Error unmark_completed(const char* path);

// NVS-backed toggle accessor. Reads fresh each call (no caching).
// Returns true when the feature is enabled.
bool completion_marking_enabled();

// Action-layer resolution used by the menu / WebUI run handlers. Turns a
// requested SD run path into the on-disk path to open, performing
// unmark-at-start when the selected file is ✓-marked. `storage` is an
// out-parameter that keeps any rewritten path alive for the caller; the
// returned pointer points either into `storage` or into `path`.
//
// Resolution is asymmetric: a bare request falls back to the ✓ copy when
// the bare name is absent; a decorated (✓) request is strict. A selected
// ✓-marked file is always unmarked before opening — never opened in place.
//
// RETURN CONTRACT:
//   - non-null: the path to open. Not-found cases return a path whose open
//     fails normally with error:66.
//   - nullptr: a required unmark-at-start failed on a file that exists. The
//     caller MUST abort the run (do not open) — opening under the ✓ name
//     could leave a false completion mark after a crash.
//
// LIFETIME CONTRACT — the returned pointer is valid only while *both*
// `storage` and whatever `path` points into are alive. In the pass-through
// cases the result aliases `path`, so a caller passing `tempString().c_str()`
// must first copy into a local std::string (e.g. Menu.h's
// get_recent_file_path / get_completed_file_path return by value).
//
// Hook this at the moment the user asserts "I'm running this file" — before
// any auto-home detour and before new InputFile(...). Auto-home paths must
// call it before stashing via `protocol_set_pending_file()` / `PendingFileRun`,
// and must check for the nullptr abort before stashing or homing.
const char* resolve_with_strip(const char* path, std::string& storage);

}  // namespace CompletionMark
