// FluidNC/src/SDFiles/SDFileTable.cpp
// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the
// LICENSE file.

#include "src/SDFiles/SDFileTable.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace sdfiles {

struct __attribute__((packed)) SDFileTable::Header {
    uint8_t  flags;
    EntryId  parent;
    uint32_t mtime;
    uint8_t  nameLen;
    // name bytes follow immediately
};

namespace {
constexpr uint8_t kLive      = 1 << 0;
constexpr uint8_t kIsDir     = 1 << 1;
constexpr uint8_t kCompleted = 1 << 2;
constexpr uint8_t kClassShift = 3;
constexpr uint8_t kClassMask  = 0x3 << kClassShift;
// Bits 5-7 of a directory's flags form a transitive content mask: bit (kContentShift+c)
// is set when the directory contains, at any depth, at least one file of class c.
constexpr uint8_t kContentShift = 5;
constexpr uint8_t kContentMask  = 0x7 << kContentShift;

// Returns the flags bit for the given class. FileClass values are 0..2, so the
// bit stays within the content mask (bits 5..7).
inline uint8_t contentBit(FileClass cls) {
    static_assert(static_cast<uint8_t>(FileClass::Config) <= 2,
                  "FileClass values exceed the content-bit range (bits 5..7)");
    return static_cast<uint8_t>(1u << (kContentShift + static_cast<uint8_t>(cls)));
}
}  // namespace

SDFileTable::SDFileTable(size_t arenaBytes, uint16_t maxEntries)
    : _arena(new uint8_t[arenaBytes + 1]),  // +1: zero-flags sentinel for compact()
      _entryOffset(new uint32_t[maxEntries]),
      _index(new EntryId[maxEntries]),
      _arenaCap(arenaBytes),
      _arenaUsed(0),
      _maxEntries(maxEntries),
      _entryCount(0),
      _indexCount(0),
      _sortMode(SortMode::NameAsc) {
    _arena[arenaBytes] = 0;  // sentinel byte: flags == 0 (not-live) for redirected tombstones
}

// Move transfers the arena buffers and counts; the mutex is non-movable, so the
// moved-to table keeps its own fresh, unlocked one. Intended for construction-time
// use (e.g. returning a built table by value); moving under concurrent access is
// not safe. Production constructs the single table in place and never moves it.
SDFileTable::SDFileTable(SDFileTable&& o) noexcept
    : _arena(std::move(o._arena)),
      _entryOffset(std::move(o._entryOffset)),
      _index(std::move(o._index)),
      _arenaCap(o._arenaCap),
      _arenaUsed(o._arenaUsed),
      _maxEntries(o._maxEntries),
      _entryCount(o._entryCount),
      _indexCount(o._indexCount),
      _sortMode(o._sortMode) {}

SDFileTable& SDFileTable::operator=(SDFileTable&& o) noexcept {
    if (this != &o) {
        _arena       = std::move(o._arena);
        _entryOffset = std::move(o._entryOffset);
        _index       = std::move(o._index);
        _arenaCap    = o._arenaCap;
        _arenaUsed   = o._arenaUsed;
        _maxEntries  = o._maxEntries;
        _entryCount  = o._entryCount;
        _indexCount  = o._indexCount;
        _sortMode    = o._sortMode;
        // _mutex is left as-is; this table keeps its own.
    }
    return *this;
}

void SDFileTable::reset() {
    _arenaUsed  = 0;
    _entryCount = 0;
    _indexCount = 0;
}

bool SDFileTable::full() const {
    // "full" when even a minimal entry (header + 1-char name) cannot be appended.
    if (_entryCount >= _maxEntries) return true;
    return _arenaUsed + sizeof(Header) + 1 > _arenaCap;
}

SDFileTable::Header* SDFileTable::headerAt(EntryId id) {
    return reinterpret_cast<Header*>(_arena.get() + _entryOffset[id]);
}
const SDFileTable::Header* SDFileTable::headerAt(EntryId id) const {
    return reinterpret_cast<const Header*>(_arena.get() + _entryOffset[id]);
}

EntryId SDFileTable::addEntry(EntryId parent, FileClass cls, bool isDir, uint32_t mtime,
                              const char* rawName) {
    bool        completed = false;
    const char* canon     = stripCompletion(rawName, completed);
    size_t      nameLen   = std::strlen(canon);
    if (nameLen > kMaxNameLen) return kInvalidEntry;
    if (_entryCount >= _maxEntries) return kInvalidEntry;
    size_t need = sizeof(Header) + nameLen;
    if (_arenaUsed + need > _arenaCap) return kInvalidEntry;

    EntryId id            = _entryCount;
    _entryOffset[id]      = static_cast<uint32_t>(_arenaUsed);
    Header* h             = headerAt(id);
    h->flags              = kLive | (isDir ? kIsDir : 0) | (completed ? kCompleted : 0) |
                            (static_cast<uint8_t>(cls) << kClassShift);
    h->parent             = parent;
    h->mtime              = mtime;
    h->nameLen            = static_cast<uint8_t>(nameLen);
    std::memcpy(_arena.get() + _arenaUsed + sizeof(Header), canon, nameLen);

    _arenaUsed += need;
    _entryCount++;
    return id;
}

bool SDFileTable::removeEntry(EntryId id) {
    if (id >= _entryCount) return false;
    headerAt(id)->flags &= ~kLive;
    return true;
}

void SDFileTable::removeSubtree(EntryId dir) {
    if (dir >= _entryCount) return;
    // Tombstone an entry only if `dir` is on its parent chain (dir itself included).
    // This reaps exactly dir's descendants, never unrelated orphans left by a direct
    // removeEntry on some other directory. The walk uses parent fields, which survive
    // tombstoning (only the live bit is cleared), so dead intermediate nodes are fine.
    // It terminates because addEntry always creates a parent before its children, so
    // parent ids strictly decrease up the chain (no cycles).
    for (uint16_t id = 0; id < _entryCount; ++id) {
        Header* h = headerAt(id);
        if (!(h->flags & kLive)) continue;
        EntryId p = id;
        while (p != kRootParent && p < _entryCount) {
            if (p == dir) {
                h->flags &= ~kLive;
                break;
            }
            p = headerAt(p)->parent;
        }
    }
}
EntryId SDFileTable::renameEntry(EntryId id, const char* newRawName) {
    if (id >= _entryCount) return kInvalidEntry;
    Header*   h     = headerAt(id);
    EntryId   par   = h->parent;
    uint32_t  mt    = h->mtime;
    bool      isdir = h->flags & kIsDir;
    FileClass cls   = static_cast<FileClass>((h->flags & kClassMask) >> kClassShift);

    h->flags &= ~kLive;                          // tombstone old first
    EntryId nw = addEntry(par, cls, isdir, mt, newRawName);
    if (nw == kInvalidEntry) {
        headerAt(id)->flags |= kLive;            // restore on failure
        return kInvalidEntry;
    }
    return nw;
}
void SDFileTable::setCompleted(EntryId id, bool completed) {
    Header* h = headerAt(id);
    if (completed) {
        h->flags |= kCompleted;
    } else {
        h->flags &= ~kCompleted;
    }
}
void SDFileTable::rebuildIndex(SortMode mode) {
    _sortMode   = mode;

    // Recompute the per-directory transitive content mask. Clear bits 5-7 on every live
    // directory, then for each live file propagate its class bit up the parent chain.
    // Parent ids strictly decrease up the chain (a parent is created before its
    // children), so each walk terminates.
    for (uint16_t id = 0; id < _entryCount; ++id) {
        Header* h = headerAt(id);
        if ((h->flags & kLive) && (h->flags & kIsDir)) h->flags &= ~kContentMask;
    }
    for (uint16_t id = 0; id < _entryCount; ++id) {
        Header* h = headerAt(id);
        if (!(h->flags & kLive) || (h->flags & kIsDir)) continue;
        uint8_t bit = contentBit(
            static_cast<FileClass>((h->flags & kClassMask) >> kClassShift));
        for (EntryId p = h->parent; p != kRootParent && p < _entryCount;
             p = headerAt(p)->parent) {
            headerAt(p)->flags |= bit;
        }
    }

    _indexCount = 0;
    for (uint16_t id = 0; id < _entryCount; ++id) {
        if (headerAt(id)->flags & kLive) _index[_indexCount++] = id;
    }
    auto* self = this;
    std::stable_sort(_index.get(), _index.get() + _indexCount,
                     [self, mode](EntryId a, EntryId b) {
        EntryId pa = self->parent(a), pb = self->parent(b);
        if (pa != pb) return pa < pb;  // group by parent
        switch (mode) {
            case SortMode::NameAsc:  return self->compareNames(a, b) < 0;
            case SortMode::NameDesc: return self->compareNames(a, b) > 0;
            case SortMode::Oldest:   return self->mtime(a) < self->mtime(b);
            case SortMode::Newest:   return self->mtime(a) > self->mtime(b);
        }
        return false;
    });
}
size_t SDFileTable::childCount(EntryId dir) const {
    size_t n = 0;
    for (uint16_t i = 0; i < _indexCount; ++i) {
        if (parent(_index[i]) == dir) ++n;
    }
    return n;
}

EntryId SDFileTable::childAt(EntryId dir, size_t i) const {
    size_t seen = 0;
    for (uint16_t k = 0; k < _indexCount; ++k) {
        if (parent(_index[k]) == dir) {
            if (seen == i) return _index[k];
            ++seen;
        }
    }
    return kInvalidEntry;
}
bool SDFileTable::dirContains(EntryId dir, FileClass cls) const {
    if (dir >= _entryCount) return false;
    const Header* h = headerAt(dir);
    if (!(h->flags & kLive) || !(h->flags & kIsDir)) return false;
    return (h->flags & contentBit(cls)) != 0;
}
EntryId SDFileTable::nextIncompleteFile(EntryId dir, EntryId after) const {
    bool past = (after == kInvalidEntry);
    for (uint16_t i = 0; i < _indexCount; ++i) {
        EntryId id = _index[i];
        if (parent(id) != dir) continue;
        if (!past) {
            if (id == after) past = true;
            continue;
        }
        const Header* h = headerAt(id);
        if ((h->flags & kIsDir) || (h->flags & kCompleted)) continue;
        return id;
    }
    return kInvalidEntry;
}
void SDFileTable::compact() {
    // Pass 1: identify tombstoned entries before memmoves overwrite their positions.
    // Mark them with a sentinel offset (high bit set; real offsets fit in 23 bits for
    // any arena <= 8 MiB, and production arenas are at most ~32 kB).
    constexpr uint32_t kDeadMark = 0x80000000u;
    for (uint16_t id = 0; id < _entryCount; ++id) {
        const Header* h = reinterpret_cast<const Header*>(
            _arena.get() + _entryOffset[id]);
        if (!(h->flags & kLive)) {
            _entryOffset[id] = kDeadMark;
        }
    }

    // Pass 2: pack live entries to the front.
    size_t write = 0;
    for (uint16_t id = 0; id < _entryCount; ++id) {
        if (_entryOffset[id] == kDeadMark) continue;
        Header* h = reinterpret_cast<Header*>(_arena.get() + _entryOffset[id]);
        size_t span = sizeof(Header) + h->nameLen;
        if (write != _entryOffset[id]) {
            std::memmove(_arena.get() + write, _arena.get() + _entryOffset[id], span);
            _entryOffset[id] = static_cast<uint32_t>(write);
        }
        write += span;
    }
    _arenaUsed = write;

    // Pass 3: redirect tombstoned entries to the zero-flags sentinel byte at
    // _arenaCap. The constructor allocates _arenaCap+1 bytes and zeroes _arena[_arenaCap],
    // so reads of headerAt(dead_id)->flags return 0 (not-live) without aliasing live data.
    for (uint16_t id = 0; id < _entryCount; ++id) {
        if (_entryOffset[id] == kDeadMark) {
            _entryOffset[id] = static_cast<uint32_t>(_arenaCap);
        }
    }
}
EntryId SDFileTable::findChild(EntryId dir, const char* name) const {
    size_t len = std::strlen(name);
    for (uint16_t id = 0; id < _entryCount; ++id) {
        const Header* h = headerAt(id);
        if (!(h->flags & kLive)) continue;
        if (h->parent != dir) continue;
        if (h->nameLen != len) continue;
        const char* nm = reinterpret_cast<const char*>(h) + sizeof(Header);
        if (std::memcmp(nm, name, len) == 0) return id;
    }
    return kInvalidEntry;
}

EntryId SDFileTable::findChild(EntryId dir, const char* name, bool completed) const {
    size_t len = std::strlen(name);
    for (uint16_t id = 0; id < _entryCount; ++id) {
        const Header* h = headerAt(id);
        if (!(h->flags & kLive)) continue;
        if (h->parent != dir) continue;
        if (h->nameLen != len) continue;
        if (static_cast<bool>(h->flags & kCompleted) != completed) continue;
        const char* nm = reinterpret_cast<const char*>(h) + sizeof(Header);
        if (std::memcmp(nm, name, len) == 0) return id;
    }
    return kInvalidEntry;
}
bool SDFileTable::fullPath(EntryId id, char* out, size_t cap,
                           bool includeCompletionMark) const {
    if (cap == 0 || id >= _entryCount) return false;

    // Collect the ancestor chain id..root (parents are created before children, so
    // parent ids strictly decrease -- no cycles).
    EntryId stack[kMaxDepth];
    size_t  depth = 0;
    for (EntryId p = id; p != kRootParent && p < _entryCount; p = parent(p)) {
        if (depth >= kMaxDepth) return false;
        stack[depth++] = p;
    }

    size_t pos = 0;
    auto put = [&](const char* s, size_t n) -> bool {
        if (pos + n + 1 > cap) return false;   // +1 reserves the NUL
        std::memcpy(out + pos, s, n);
        pos += n;
        return true;
    };

    // Emit root-first ("/name" per level); the basename is the last (k == 0) element.
    for (size_t k = depth; k-- > 0; ) {
        EntryId e = stack[k];
        if (!put("/", 1)) return false;
        const Header* h = headerAt(e);
        // Re-insert the on-disk checkmark on ANY component carrying the completed
        // bit, not just the leaf: a directory whose on-disk name begins with the
        // mark is stored canonical with the bit set, and its files only open when
        // the path restores the mark on that directory component too.
        if (includeCompletionMark && (h->flags & kCompleted)) {
            if (!put(kCompletionPrefix, kCompletionPrefixLen)) return false;
        }
        const char* nm = reinterpret_cast<const char*>(h) + sizeof(Header);
        if (!put(nm, h->nameLen)) return false;
    }
    out[pos] = '\0';
    return true;
}
int SDFileTable::compareNames(EntryId a, EntryId b) const {
    const Header* ha = headerAt(a);
    const Header* hb = headerAt(b);
    const char* na = reinterpret_cast<const char*>(ha) + sizeof(Header);
    const char* nb = reinterpret_cast<const char*>(hb) + sizeof(Header);
    size_t la = ha->nameLen, lb = hb->nameLen;
    size_t n  = (la < lb) ? la : lb;
    int    c  = std::memcmp(na, nb, n);
    if (c != 0) return c;
    return (la < lb) ? -1 : (la > lb) ? 1 : 0;
}

bool      SDFileTable::isDir(EntryId id) const { return headerAt(id)->flags & kIsDir; }
bool      SDFileTable::isLive(EntryId id) const { return headerAt(id)->flags & kLive; }
bool      SDFileTable::isCompleted(EntryId id) const {
    return headerAt(id)->flags & kCompleted;
}
FileClass SDFileTable::fileClass(EntryId id) const {
    return static_cast<FileClass>((headerAt(id)->flags & kClassMask) >> kClassShift);
}
uint32_t  SDFileTable::mtime(EntryId id) const { return headerAt(id)->mtime; }
EntryId   SDFileTable::parent(EntryId id) const { return headerAt(id)->parent; }

const char* SDFileTable::name(EntryId id) const {
    // name bytes are not NUL-terminated in the arena; copy into a per-instance scratch.
    static thread_local char scratch[kMaxNameLen + 1];
    const Header* h = headerAt(id);
    std::memcpy(scratch, reinterpret_cast<const uint8_t*>(h) + sizeof(Header), h->nameLen);
    scratch[h->nameLen] = '\0';
    return scratch;
}

void SDFileTable::copyName(EntryId id, char* out, size_t cap) const {
    if (cap == 0) return;
    const Header* h = headerAt(id);
    size_t n = h->nameLen;
    if (n > cap - 1) n = cap - 1;
    std::memcpy(out, reinterpret_cast<const uint8_t*>(h) + sizeof(Header), n);
    out[n] = '\0';
}

size_t SDFileTable::liveCount() const {
    size_t n = 0;
    for (uint16_t id = 0; id < _entryCount; ++id) {
        if (headerAt(id)->flags & kLive) ++n;
    }
    return n;
}

const char* SDFileTable::stripCompletion(const char* rawName, bool& completedOut) {
    if (std::strncmp(rawName, kCompletionPrefix, kCompletionPrefixLen) == 0) {
        completedOut = true;
        return rawName + kCompletionPrefixLen;
    }
    completedOut = false;
    return rawName;
}

}  // namespace sdfiles
