// FluidNC/src/SDFiles/SDScan.cpp
// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the
// LICENSE file.

#include "src/SDFiles/SDScan.h"
#include "src/SDFiles/SDMenu.h"

#include <cstddef>
#include <cstring>

namespace sdfiles {
namespace SDScan {

bool addScannedEntry(SDFileTable& t, const char* relPath, bool isDir,
                     uint32_t mtime, FileClass cls) {
    if (relPath == nullptr || relPath[0] != '/') return false;
    const char* lastSlash = std::strrchr(relPath, '/');
    if (lastSlash == nullptr) return false;
    const char* basename = lastSlash + 1;
    if (basename[0] == '\0') return false;  // trailing slash -> no basename
    EntryId parent;
    if (lastSlash == relPath) {
        parent = kRootParent;  // child of root (e.g. "/a.gcode" or "/jobs")
    } else {
        size_t prefixLen = static_cast<size_t>(lastSlash - relPath);
        if (prefixLen >= 1024) return false;
        char prefixBuf[1024];
        std::memcpy(prefixBuf, relPath, prefixLen);
        prefixBuf[prefixLen] = '\0';
        parent = SDMenu::resolvePath(t, prefixBuf);
        if (parent == kInvalidEntry) return false;
    }
    EntryId id = t.addEntry(parent, cls, isDir, mtime, basename);
    return id != kInvalidEntry;
}

bool isHiddenPath(const char* relPath) {
    if (relPath == nullptr) return false;
    // Hidden first component: path begins with "/.".
    if (relPath[0] == '/' && relPath[1] == '.') return true;
    // Hidden leaf: basename (after the last '/') begins with '.'.
    const char* lastSlash = std::strrchr(relPath, '/');
    if (lastSlash != nullptr && lastSlash[1] == '.') return true;
    return false;
}

bool addOrReplaceEntry(SDFileTable& t, const char* relPath, bool isDir,
                       uint32_t mtime, FileClass cls) {
    if (relPath == nullptr || relPath[0] != '/') return false;
    if (isHiddenPath(relPath)) return false;
    const char* lastSlash = std::strrchr(relPath, '/');
    if (lastSlash == nullptr) return false;
    const char* basename = lastSlash + 1;
    if (basename[0] == '\0') return false;  // trailing slash -> no basename

    // Idempotent overwrite: drop any existing live entry at this exact path first.
    EntryId existing = SDMenu::resolvePath(t, relPath);
    if (existing != kInvalidEntry) {
        t.removeEntry(existing);
    }

    // Resolve the parent directory chain, creating missing directories as we go.
    EntryId parent = kRootParent;
    const char* p = relPath + 1;
    while (p < lastSlash) {
        const char* end = p;
        while (end < lastSlash && *end != '/') ++end;
        size_t compLen = static_cast<size_t>(end - p);
        if (compLen == 0 || compLen > kMaxNameLen) return false;
        char buf[kMaxNameLen + 1];
        std::memcpy(buf, p, compLen);
        buf[compLen] = '\0';
        EntryId child = t.findChild(parent, buf);
        if (child == kInvalidEntry) {
            child = t.addEntry(parent, FileClass::Gcode, /*isDir=*/true, 0, buf);
            if (child == kInvalidEntry) {
                t.compact();
                child = t.addEntry(parent, FileClass::Gcode, true, 0, buf);
                if (child == kInvalidEntry) return false;
            }
        }
        parent = child;
        p = (end < lastSlash) ? end + 1 : end;
    }

    // Append the leaf, with one compact-then-retry on a full arena.
    EntryId id = t.addEntry(parent, cls, isDir, mtime, basename);
    if (id == kInvalidEntry) {
        t.compact();
        id = t.addEntry(parent, cls, isDir, mtime, basename);
    }
    return id != kInvalidEntry;
}

bool removeEntryByPath(SDFileTable& t, const char* relPath) {
    EntryId id = SDMenu::resolvePath(t, relPath);
    if (id == kInvalidEntry) return false;
    return t.removeEntry(id);
}

bool removeSubtreeByPath(SDFileTable& t, const char* relPath) {
    EntryId id = SDMenu::resolvePath(t, relPath);
    if (id == kInvalidEntry) return false;
    t.removeSubtree(id);
    return true;
}

bool applyCompletionTransition(SDFileTable& t, const char* sourceRel,
                               const char* destRel) {
    // Determine the new completed state from the leaf basename of destRel.
    bool newCompleted = false;
    if (destRel != nullptr) {
        const char* lastSlash = std::strrchr(destRel, '/');
        const char* leaf = (lastSlash != nullptr) ? lastSlash + 1 : destRel;
        newCompleted = (std::strncmp(leaf, kCompletionPrefix, kCompletionPrefixLen) == 0);
    }

    // Resolve the source entry (pre-transition path). Nothing to do if absent.
    EntryId src = SDMenu::resolvePath(t, sourceRel);
    if (src == kInvalidEntry) return false;

    // Resolve the twin (the existing entry at the destination path) unconditionally,
    // BEFORE any mutation, while src still carries its old completion state and differs
    // from the twin. The arena determines whether a same-canonical twin exists; disk
    // state (clobbered or not) is irrelevant — stale twins must be collapsed regardless.
    EntryId twin = SDMenu::resolvePath(t, destRel);

    // Remove the twin if it exists and is distinct from src.
    bool removed = false;
    if (twin != kInvalidEntry && twin != src) {
        t.removeEntry(twin);
        removed = true;
    }

    // Flip the source entry to its new completion state.
    t.setCompleted(src, newCompleted);

    return removed;
}

}  // namespace SDScan
}  // namespace sdfiles
