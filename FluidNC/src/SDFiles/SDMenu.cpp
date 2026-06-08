// FluidNC/src/SDFiles/SDMenu.cpp
// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the
// LICENSE file.

#include "src/SDFiles/SDMenu.h"

#include <cstddef>
#include <cstring>

namespace SDMenu {

using sdfiles::EntryId;
using sdfiles::kInvalidEntry;
using sdfiles::kRootParent;
using sdfiles::kCompletionPrefix;
using sdfiles::kCompletionPrefixLen;

// Strips the leading checkmark prefix from a basename component if present.
// Advances `start` and decrements `len` past the prefix bytes. Returns true
// when the prefix was present (the component carried the completion mark).
static bool stripCompletionInPlace(const char*& start, size_t& len) {
    if (len >= kCompletionPrefixLen &&
        std::memcmp(start, kCompletionPrefix, kCompletionPrefixLen) == 0) {
        start += kCompletionPrefixLen;
        len   -= kCompletionPrefixLen;
        return true;
    }
    return false;
}

EntryId resolvePath(const sdfiles::SDFileTable& t, const char* path) {
    if (path == nullptr || path[0] == '\0') return kInvalidEntry;
    // Walk components separated by '/'. A leading '/' is required by the format
    // and consumed at the start. A trailing '/' is tolerated.
    const char* p = path;
    if (*p != '/') return kInvalidEntry;
    ++p;
    EntryId cur = kRootParent;
    while (*p != '\0') {
        // Find the next '/' or end-of-string; the slice [p, end) is one component.
        const char* end = p;
        while (*end != '\0' && *end != '/') ++end;
        size_t componentLen = static_cast<size_t>(end - p);
        if (componentLen == 0) {
            // Empty component (e.g. trailing slash, or "//"). Trailing is OK; we
            // accept it and stop walking.
            if (*end == '\0') break;
            return kInvalidEntry;  // mid-path empty component is malformed
        }
        const char* compStart = p;
        size_t      compLen   = componentLen;
        bool hadCheckmark = stripCompletionInPlace(compStart, compLen);
        // findChild takes a NUL-terminated C string; copy the component into a
        // small stack buffer (basenames are bounded by kMaxNameLen = 255).
        if (compLen > sdfiles::kMaxNameLen) return kInvalidEntry;
        char buf[sdfiles::kMaxNameLen + 1];
        std::memcpy(buf, compStart, compLen);
        buf[compLen] = '\0';
        EntryId next = t.findChild(cur, buf, hadCheckmark);
        if (next == kInvalidEntry) return kInvalidEntry;
        cur = next;
        p = (*end == '\0') ? end : end + 1;
    }
    return cur;
}

}  // namespace SDMenu
