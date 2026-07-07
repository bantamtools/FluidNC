// Copyright (c) 2026 - Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in
// the LICENSE file.
//
//  Persistent pause-instruction wire contract — pure, host-testable core.
//
// WIRE GRAMMAR (pinned; Studio's LineClassifier matches these byte-for-byte).
// The emit site is GCode.cpp :: emit_pause_instruction() (co-located contract
// comment there). Emitted via the no-space report-token mechanism, NOT a log_*
// macro (whose "[MSG:INFO: " prefix carries a trailing space).
//
//   SET    : [MSG:INSTR:<text>]   -- set the current persistent pause instruction
//                                    (NO space after the final colon)
//   CLEAR  : [MSG:INSTRCLR]       -- clear it (distinct token; NOT an empty SET)
//   NOTICE : [MSG:NOTICE:<text>]  -- RESERVED for the deferred transient-notices
//                                    feature; NOT emitted in v1.
//
// <text> is produced by sanitize_pause_instruction(): markup/control bytes
// stripped, UTF-8-codepoint-safe truncation to PAUSE_INSTR_MAX bytes with a
// literal ASCII "..." suffix when truncated. Studio renders it Text.PlainText —
// that PlainText render is the real security boundary; this sanitizer is belt.

#pragma once

#include <cstddef>
#include <string>

// Exact wire tokens — single source of truth for the byte grammar.
inline constexpr char PAUSE_INSTR_SET_PREFIX[]  = "[MSG:INSTR:";   // + <text> + "]"
inline constexpr char PAUSE_INSTR_CLEAR_TOKEN[] = "[MSG:INSTRCLR]";

// Max bytes of payload text (excluding the trailing NUL) we emit/cache.
constexpr size_t PAUSE_INSTR_MAX   = 64;
// Required capacity of every cache/scratch buffer passed to the helpers below.
constexpr size_t PAUSE_INSTR_BUFSZ = PAUSE_INSTR_MAX + 1;  // 65, mirrors pending_m0_comment[65]

// Sanitize `in` into `out` (capacity PAUSE_INSTR_BUFSZ). Strips ']','<','>','&',
// newlines, and C0/DEL control chars; truncates on a UTF-8 codepoint boundary at
// PAUSE_INSTR_MAX bytes and appends literal ASCII "..." when truncated. `out` is
// always NUL-terminated and never written past index PAUSE_INSTR_MAX. `in` may be
// null (treated as empty).
void sanitize_pause_instruction(const char* in, char* out);

// Cache state machine (pure — operates on a caller-owned [PAUSE_INSTR_BUFSZ]
// buffer). GCode.cpp owns the real module-scope cache and wraps these with the
// actual channel emit.

// Sanitize `text` into `cache`. After this, pause_instruction_active(cache) is
// true iff a non-empty instruction remained after sanitization.
void pause_instruction_set(char* cache, const char* text);

// True iff `cache` currently holds a non-empty instruction.
bool pause_instruction_active(const char* cache);

// If `cache` holds an instruction, empty it and return true (caller should emit
// [MSG:INSTRCLR]); if already empty, leave it and return false (caller emits
// nothing). Returning false on an empty cache is what makes a bare user
// feed-hold resume a no-op instead of spamming a clear on every resume (§4.3).
bool pause_instruction_clear(char* cache);

// Build the exact SET wire bytes for `text` into `out`:
//   PAUSE_INSTR_SET_PREFIX + text + "]"  ==  "[MSG:INSTR:<text>]".
// `text` is assumed already sanitized (the cache always is).
void format_instr_line(const char* text, std::string& out);
