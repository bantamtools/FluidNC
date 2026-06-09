// FluidNC/src/SDFiles/SDCacheListing.h
// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the
// LICENSE file.
//
// Serializes the SD menu's SDFileTable arena into the compact, count-framed,
// line-based "SDCACHE" listing format consumed by BT_SVG. Pure C++; no transport
// or hardware dependencies; host-testable.
//
// ZERO HEAP: the walk uses a fixed on-stack traversal stack and a fixed on-stack
// line buffer, so it allocates nothing on the heap regardless of file count. Each
// finished line is handed to a ListingSink. This matters on the device: a large
// card fills the arena to its cap, and building the whole listing in one growing
// std::string previously exhausted the fragmented heap (operator new -> bad_alloc
// -> abort). Streaming line-by-line keeps peak memory bounded.
//
// LOCKING: the caller MUST hold table.mutex() for the duration of the call. The
// walk only reads the arena; the sink decides how each line is delivered.

#pragma once

#include "src/SDFiles/SDFileTable.h"

namespace sdfiles {

// Maps a SortMode to its wire token (one token per order; never ambiguous).
const char* sortToken(SortMode mode);

// Receives one listing line at a time, WITHOUT a trailing newline. The
// implementation decides how to deliver and terminate it (serial channel, HTTP
// chunk, or a test buffer). Called once per line, in emission order.
class ListingSink {
public:
    virtual ~ListingSink() = default;
    virtual void line(const char* text) = 0;
};

// Streams the listing to `sink`: a header line, one line per live entry in the
// table's sort order and pre-order (each directory immediately followed by its
// subtree), then a terminator line. No heap allocation in the walk. The caller
// MUST hold table.mutex() across the call.
void serializeMenuCache(const SDFileTable& table, ListingSink& sink);

// Streams the well-formed empty listing (n=0). Used when no SD-menu arena is
// available (no OLED / legacy backend). Matches serializeMenuCache() on an empty,
// freshly-built table.
void serializeEmptyMenuCache(ListingSink& sink);

}  // namespace sdfiles
