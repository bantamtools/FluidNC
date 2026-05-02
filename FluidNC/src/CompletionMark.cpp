// CompletionMark.cpp — see CompletionMark.h for the design.

#include "CompletionMark.h"

#include <cstring>
#include <exception>
#include <filesystem>
#include <string>

#include "FluidPath.h"
#include "Logging.h"
#include "Machine/MachineConfig.h"  // config->_oled->_menu for menu state sync
#include "Menu.h"
#include "OLED.h"
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

// Performs the rename on the SD via std::filesystem. If `dest` already
// exists, uses the temp-rename dance to avoid the data-destructive
// failure mode of delete-first.
//
// Steps when dest exists:
//   rename(src, src + ".completionmark.tmp")
//   remove(dest)
//   rename(src + ".completionmark.tmp", dest)
//
// At every step, the source's data exists somewhere on disk under a
// known name — recoverable on transient SD I/O failure.
Error rename_with_clobber(const char* src, const char* dest) {
    try {
        FluidPath srcPath { src, "sd" };
        FluidPath dstPath { dest, "sd" };
        if (std::filesystem::exists(dstPath)) {
            log_info("CompletionMark: clobbering existing " << dest);
            std::string tmp = std::string(src) + ".completionmark.tmp";
            FluidPath  tmpPath { tmp.c_str(), "sd" };
            std::filesystem::rename(srcPath, tmpPath);
            std::filesystem::remove(dstPath);
            std::filesystem::rename(tmpPath, dstPath);
        } else {
            std::filesystem::rename(srcPath, dstPath);
        }
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

// After a successful on-disk rename, propagate the change into the
// in-memory file menu so the file browser shows the new name without a
// re-scan. Best-effort — if the entry isn't in the menu (file added
// outside our scan path, menu rebuilt mid-flight, etc.), log and
// continue. The disk is the source of truth.
static void sync_menu_after_rename(const char* old_path, const char* new_path) {
    if (config == nullptr || config->_oled == nullptr || config->_oled->_menu == nullptr) {
        return;
    }
    // FileStream::path() returns the fully-qualified canonical form
    // (e.g. "/sd/foo.gcode") via FluidPath, but the menu cache keys
    // entries by base_path-relative form ("/foo.gcode"). Strip the
    // mount prefix before the lookup so the strcmp in
    // Menu::rename_sd_file_entry actually matches. Without this
    // strip the lookup silently fails and the menu never reflects
    // the rename until the next full populate. (Issue.)
    const char* old_rel = strip_sd_prefix(old_path);
    const char* new_rel = strip_sd_prefix(new_path);
    if (!config->_oled->_menu->rename_sd_file_entry(old_rel, new_rel)) {
        log_info("CompletionMark: menu entry not found for " << old_path
                 << " (renamed on disk to " << new_path
                 << "); browser will reflect the change on next scan");
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
    Error e = rename_with_clobber(path, marked.c_str());
    if (e == Error::Ok) {
        sync_menu_after_rename(path, marked.c_str());
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
    Error e = rename_with_clobber(path, unmarked.c_str());
    if (e == Error::Ok) {
        sync_menu_after_rename(path, unmarked.c_str());
    }
    return e;
}

const char* resolve_with_strip(const char* path, std::string& storage) {
    if (path == nullptr || *path == '\0') return path;
    if (!completion_marking_enabled()) return path;
    const char*  slash = std::strrchr(path, '/');
    const size_t off   = slash ? static_cast<size_t>((slash - path) + 1) : 0;
    if (!has_completion_prefix(path + off)) return path;
    if (unmark_completed(path) != Error::Ok) return path;
    storage = compute_unmarked_path(path);
    return storage.empty() ? path : storage.c_str();
}

}  // namespace CompletionMark
