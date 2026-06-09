// FluidNC/src/SDFiles/SDCacheListing.cpp
// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the
// LICENSE file.

#include "src/SDFiles/SDCacheListing.h"

#include <cstdio>   // snprintf
#include <cstring>  // memcpy

namespace {

char classChar(sdfiles::FileClass cls) {
    switch (cls) {
        case sdfiles::FileClass::Gcode:    return 'g';
        case sdfiles::FileClass::Firmware: return 'f';
        case sdfiles::FileClass::Config:   return 'c';
    }
    return '?';  // defensive; unreachable for valid enumerators
}

}  // namespace

namespace sdfiles {

const char* sortToken(SortMode mode) {
    switch (mode) {
        case SortMode::NameAsc:  return "name_asc";
        case SortMode::NameDesc: return "name_desc";
        case SortMode::Oldest:   return "date_old";
        case SortMode::Newest:   return "date_new";
    }
    return "name_asc";  // defensive; unreachable for valid enumerators
}

void serializeEmptyMenuCache(ListingSink& sink) {
    sink.line("SDCACHE-BEGIN n=0 sort=name_asc complete=1");
    sink.line("SDCACHE-END n=0");
}

void serializeMenuCache(const SDFileTable& table, ListingSink& sink) {
    // One reusable on-stack line buffer: type|class|depth| + checkmark + name.
    // Sized for the longest possible name plus the fixed prefix and the header.
    char buf[kMaxNameLen + 64];

    snprintf(buf, sizeof(buf), "SDCACHE-BEGIN n=%u sort=%s complete=%d",
             static_cast<unsigned>(table.liveCount()), sortToken(table.sortMode()),
             table.scanComplete() ? 1 : 0);
    sink.line(buf);

    // Iterative pre-order DFS over the sorted tree using a fixed on-stack stack.
    // No heap: depth is bounded by kMaxDepth, so the stack array is bounded too.
    // childAt() yields each directory's children in the table's sort order, so the
    // emission is sorted pre-order (each directory line followed by its subtree).
    struct Frame {
        EntryId dir;
        size_t  next;
        size_t  count;
    };
    Frame  stack[kMaxDepth + 2];
    int    sp     = 0;
    stack[0]      = { kRootParent, 0, table.childCount(kRootParent) };

    while (sp >= 0) {
        Frame& f = stack[sp];
        if (f.next >= f.count) {
            --sp;
            continue;
        }
        EntryId id = table.childAt(f.dir, f.next++);
        if (id == kInvalidEntry) {
            continue;  // defensive: index changed under us
        }

        const bool dir = table.isDir(id);

        // type | class | depth | basename
        char* p = buf;
        *p++    = dir ? 'D' : 'F';
        *p++    = '|';
        *p++    = dir ? '-' : classChar(table.fileClass(id));
        *p++    = '|';
        p += snprintf(p, 8, "%d", sp);  // depth == current stack level
        *p++ = '|';

        // Real on-disk basename: re-prepend the completion prefix when set.
        if (!dir && table.isCompleted(id)) {
            memcpy(p, kCompletionPrefix, kCompletionPrefixLen);  // UTF-8 checkmark
            p += kCompletionPrefixLen;
        }
        table.copyName(id, p, sizeof(buf) - static_cast<size_t>(p - buf));
        sink.line(buf);

        if (dir && sp + 1 <= static_cast<int>(kMaxDepth)) {
            stack[++sp] = { id, 0, table.childCount(id) };
        }
    }

    snprintf(buf, sizeof(buf), "SDCACHE-END n=%u",
             static_cast<unsigned>(table.liveCount()));
    sink.line(buf);
}

}  // namespace sdfiles
