// CompletionMark.cpp — see CompletionMark.h for the design.

#include "CompletionMark.h"

#include <cstring>
#include <exception>
#include <filesystem>
#include <mutex>
#include <string>

#include "FluidPath.h"
#include "Logging.h"
#include "Machine/MachineConfig.h"  // config->_oled->_menu for menu state sync
#include "Menu.h"
#include "OLED.h"
#include "SDFiles/MenuSortConfig.h"  // sdfiles::menuSortMode()
#include "SDFiles/SDFileTable.h"     // sd_table().mutex() — arena lock
#include "SDFiles/SDScan.h"          // sdfiles::SDScan::applyCompletionTransition
#include "SettingsDefinitions.h"

namespace CompletionMark {

bool completion_marking_enabled() {
    // Read fresh each call. Two reads per job at most; no caching.
    // Null guard handles the early-boot window before make_settings()
    // has run; treat as disabled rather than crash.
    return completion_marking != nullptr && completion_marking->get() != 0;
}


bool has_completion_prefix(const char* basename) {
    if (basename == nullptr) {
        return false;
    }
    return std::strncmp(basename, kPrefix, kPrefixLen) == 0;
}

namespace {

// Returns the index of the basename within `path` (one past the last
// '/'). Returns 0 if there is no '/'.
size_t basename_offset(const char* path) {
    const char* slash = std::strrchr(path, '/');
    return (slash == nullptr) ? 0 : (slash - path) + 1;
}

}  // namespace

std::string compute_marked_path(const char* path) {
    if (path == nullptr) {
        return {};
    }
    const size_t off = basename_offset(path);
    if (has_completion_prefix(path + off)) {
        return path;  // already marked, no-op
    }
    std::string out;
    out.reserve(std::strlen(path) + kPrefixLen);
    out.assign(path, off);
    out.append(kPrefix, kPrefixLen);
    out.append(path + off);
    return out;
}

std::string compute_unmarked_path(const char* path) {
    if (path == nullptr) {
        return {};
    }
    const size_t off = basename_offset(path);
    if (!has_completion_prefix(path + off)) {
        return path;  // already unmarked, no-op
    }
    // Empty-result guard: basename is exactly the prefix.
    if (path[off + kPrefixLen] == '\0') {
        return {};
    }
    std::string out;
    out.reserve(std::strlen(path) - kPrefixLen);
    out.assign(path, off);
    out.append(path + off + kPrefixLen);
    return out;
}

namespace {

// True if `path` (relative to the SD mount) exists on disk. Swallows
// mount/IO errors as "absent" so resolution degrades to a normal
// not-found rather than throwing.
bool sd_path_exists(const char* path) {
    try {
        FluidPath p { path, "sd" };
        return std::filesystem::exists(p);
    } catch (const std::exception&) {
        return false;
    } catch (const Error) {
        return false;
    }
}

// Inverted two-step clobber. Remove any existing destination (the
// dead-weight loser) first, then rename the source (the winner) into
// place. The winner never moves until the loser is gone, so an
// interruption leaves the full file under one name or the other — never a
// stranded temp. No temp file is created. When the destination does not
// exist this is a single rename, atomic to the medium's limit.
//
// `destRemoved` reports whether the destination was deleted from disk. On a
// failure after the remove, the caller uses it to drop the now-absent dest
// entry from the menu so the menu still matches disk.
Error rename_with_clobber(const char* src, const char* dest, bool& destRemoved) {
    destRemoved = false;
    try {
        FluidPath srcPath { src, "sd" };
        FluidPath dstPath { dest, "sd" };
        if (std::filesystem::exists(dstPath)) {
            log_info("CompletionMark: clobbering existing " << dest);
            std::filesystem::remove(dstPath);
            destRemoved = true;
        }
        std::filesystem::rename(srcPath, dstPath);
        return Error::Ok;
    } catch (const std::exception& e) {
        log_warn("CompletionMark: rename " << src << " -> " << dest
                 << " failed: " << e.what());
        return Error::FsFailedRenameFile;
    } catch (const Error err) {
        log_warn("CompletionMark: rename " << src << " -> " << dest
                 << " failed (Error " << static_cast<int>(err) << ")");
        return Error::FsFailedRenameFile;
    }
}

}  // namespace

// Strip the leading "/sd" mount prefix from a fully-qualified SD
// path so the result matches the form the menu cache stores. The
// populate function in esp32/sdmmc.cpp / sdspi.cpp builds cache
// keys via `full_path.substr(strlen(base_path))` where `base_path`
// is "/sd" (no trailing slash), so the stored form is e.g.
// "/foo.gcode" -- with the leading slash preserved. Match "/sd"
// only when followed by '/' or end-of-string so paths like
// "/sdcard/..." don't get mis-stripped. Returns the input
// unchanged when the prefix isn't present.
static const char* strip_sd_prefix(const char* p) {
    if (p == nullptr) return p;
    if (strncmp(p, "/sd", 3) != 0) return p;
    char tail = p[3];
    if (tail != '/' && tail != '\0') return p;
    return p + 3;
}

// After a successful on-disk rename, propagates the completion state change into the
// arena (SDFileTable). `source_path` is the pre-rename fully-qualified path;
// `dest_path` is the post-rename path.
//
// When a same-canonical twin is collapsed, the sorted index changes, so rebuildIndex
// and a full repaint are triggered. When only the completed bit flips (no removal),
// the sort position is unchanged and no rebuild is needed.
static void sync_menu_after_transition(const char* source_path,
                                       const char*  dest_path) {
    if (config == nullptr || config->_oled == nullptr || config->_oled->_menu == nullptr) {
        return;
    }
    const char* source_rel = strip_sd_prefix(source_path);
    const char* dest_rel   = strip_sd_prefix(dest_path);
    bool indexChanged;
    {
        std::lock_guard<std::recursive_mutex> lk(config->_oled->_menu->sd_table().mutex());
        indexChanged = sdfiles::SDScan::applyCompletionTransition(
            config->_oled->_menu->sd_table(), source_rel, dest_rel);
        if (indexChanged) {
            config->_oled->_menu->sd_table().rebuildIndex(sdfiles::menuSortMode());
        }
    }
    if (indexChanged) {
        config->_oled->refresh_display(true);
    }
}

// When a clobber removed the destination on disk but the rename then failed,
// the dest file is gone yet the menu still lists it. Drop the stale entry so
// the menu matches disk. Mirrors sync_menu_after_transition's locking and
// refresh.
static void remove_menu_entry(const char* dest_path) {
    if (config == nullptr || config->_oled == nullptr || config->_oled->_menu == nullptr) {
        return;
    }
    const char* dest_rel = strip_sd_prefix(dest_path);
    bool removed;
    {
        std::lock_guard<std::recursive_mutex> lk(config->_oled->_menu->sd_table().mutex());
        removed = sdfiles::SDScan::removeEntryByPath(
            config->_oled->_menu->sd_table(), dest_rel);
        if (removed) {
            config->_oled->_menu->sd_table().rebuildIndex(sdfiles::menuSortMode());
        }
    }
    if (removed) {
        config->_oled->refresh_display(true);
    }
}

Error mark_completed(const char* path) {
    if (path == nullptr || *path == '\0') {
        return Error::InvalidValue;
    }
    std::string marked = compute_marked_path(path);
    if (marked.empty() || marked == path) {
        // No-op (already marked or invalid input).
        return Error::Ok;
    }
    bool  destRemoved = false;
    Error e           = rename_with_clobber(path, marked.c_str(), destRemoved);
    if (e == Error::Ok) {
        sync_menu_after_transition(path, marked.c_str());
    } else if (destRemoved) {
        // Destination deleted but the rename did not complete; drop the
        // now-absent dest entry so the menu matches disk.
        remove_menu_entry(marked.c_str());
    }
    return e;
}

Error unmark_completed(const char* path) {
    if (path == nullptr || *path == '\0') {
        return Error::InvalidValue;
    }
    std::string unmarked = compute_unmarked_path(path);
    if (unmarked.empty()) {
        // Strip would leave empty basename — refuse.
        log_warn("CompletionMark: refusing to unmark " << path
                 << " — strip would yield empty basename");
        return Error::InvalidValue;
    }
    if (unmarked == path) {
        // Already unmarked.
        return Error::Ok;
    }
    bool  destRemoved = false;
    Error e           = rename_with_clobber(path, unmarked.c_str(), destRemoved);
    if (e == Error::Ok) {
        sync_menu_after_transition(path, unmarked.c_str());
    } else if (destRemoved) {
        remove_menu_entry(unmarked.c_str());
    }
    return e;
}

// Resolves a requested SD run path to the on-disk path to open, performing
// unmark-at-start if the selected physical file is marked.
//
// Resolution is intentionally asymmetric:
//   - A bare request (name) resolves loosely: if `name` is absent on disk,
//     fall back to the marked counterpart `✓name`. Bare names are the
//     natural identity a host or operator types, so re-running a file works
//     regardless of its current mark state.
//   - A decorated request (✓name) is strict: it never falls back to the
//     unmarked name. The only producer of decorated requests is the menu,
//     which already knows the exact on-disk name.
// In every case, a selected ✓-marked file undergoes unmark-at-start
// (✓name -> name, open name) before opening — resolution never opens a
// ✓-marked file in place. Unmark-at-start exists to keep the mark a
// trustworthy completion indicator: flipping the file to its unmarked name
// before any output means a crash mid-plot leaves it truthfully unmarked.
//
// Returns nullptr when a required unmark-at-start fails on a file that
// exists: the caller MUST abort the run rather than open. Returning a clear
// failure (instead of the marked path) is deliberate — collapsing it back
// to returning the marked path would silently reintroduce the
// false-completion-mark hazard. Not-found cases instead return a path whose
// open fails normally with error:66.
const char* resolve_with_strip(const char* path, std::string& storage) {
    if (path == nullptr || *path == '\0') return path;
    if (!completion_marking_enabled()) return path;

    const size_t off          = basename_offset(path);
    const bool   requestMarked = has_completion_prefix(path + off);

    if (requestMarked) {
        // Decorated request: strict. Target the marked name exactly.
        if (!sd_path_exists(path)) {
            // Marked file absent — nothing to unmark. Let the caller's open
            // fail normally (error:66); not an abort.
            return path;
        }
        if (unmark_completed(path) != Error::Ok) return nullptr;
        storage = compute_unmarked_path(path);
        return storage.empty() ? path : storage.c_str();
    }

    // Bare request: prefer the unmarked name; fall back to the marked
    // counterpart only when the unmarked name is absent.
    if (sd_path_exists(path)) {
        return path;  // unmarked file present — open as-is, mark on completion
    }
    std::string marked = compute_marked_path(path);
    if (marked.empty() || !sd_path_exists(marked.c_str())) {
        return path;  // neither name exists — open fails normally (error:66)
    }
    if (unmark_completed(marked.c_str()) != Error::Ok) return nullptr;
    storage = compute_unmarked_path(marked.c_str());
    return storage.empty() ? path : storage.c_str();
}

}  // namespace CompletionMark
