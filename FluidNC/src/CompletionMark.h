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

// On-disk rename. Best-effort: returns the rename layer's Error code on
// failure. Caller logs and proceeds. Uses the temp-rename dance when
// the destination exists.
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

// Action-layer convenience used by the menu / WebUI handlers. If the
// toggle is enabled and `path`'s basename has the completion prefix,
// strip it on disk and return the stripped path. Otherwise return
// `path` unchanged. `storage` is an out-parameter that keeps the
// stripped std::string alive for the caller's lifetime; the returned
// const char* points either into `storage` or into `path`.
//
// LIFETIME CONTRACT — the returned pointer is valid only while *both*
// of these are alive:
//   - `storage` (the std::string passed in by the caller), AND
//   - whatever `path` points into.
// In the no-strip case the returned pointer aliases `path`, so if
// `path` was `tempString().c_str()` the result dangles. Callers who
// have a getter that returns `std::string` by value (e.g. Menu.h's
// get_recent_file_path / get_completed_file_path) MUST copy into a
// local std::string first and pass its c_str() — do not chain
// `.c_str()` directly on a temporary.
//
// Best-effort: any rename failure leaves `path` unchanged, so the job
// still runs.
//
// Hook this at the moment the user takes the action that asserts
// "I'm running this file" — before any auto-home detour, before
// `new InputFile(...)`. Auto-home paths must call this before stashing
// the path via set_file_awaiting_homing(), so the eventual InputFile
// (constructed in OLED.cpp after homing completes) sees the
// already-stripped name.
const char* resolve_with_strip(const char* path, std::string& storage);

}  // namespace CompletionMark
