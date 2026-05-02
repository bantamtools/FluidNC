// SDMenuEvents.h
//
// Incremental SD file-menu refresh primitives. Replaces the
// previous "every user-initiated SD mutation triggers a full
// sd_populate_files_menu() rescan" pattern, which on a populated
// card takes 10s of seconds and is user-interrupting.
//
// Layered:
// - Pure helpers (this file's namespace): host-testable. Path
//   normalization, extension allowlist, subtree predicate.
// - SD-event entry points (declared here, implemented in
//   FluidNC/esp32/sdmmc.cpp + sdspi.cpp next to the existing
//   sd_populate_files_menu): guard on OLED + mount, strip the
//   "/sd/" prefix, dispatch into Menu cache primitives.
//
// Issues: internal tracker.
// Design: docs/plans/2026-04-29-sd-menu-incremental-refresh-design.md

#pragma once

#include <cstddef>

namespace SDMenuEvents {

// Strip the leading "/sd/" prefix from a fully-qualified SD path.
// Returns a pointer into the original buffer (no allocation). If
// the input does not start with "/sd/" returns nullptr — caller
// must check.
const char* strip_sd_prefix(const char* full_path);

// Three classes of menu-eligible files on SD. Mirrors the
// branching in sd_populate_files_menu (esp32/sdmmc.cpp): gcode-like
// files go to _files_menu, .bin firmware to _firmware_menu,
// .yaml configs to _config_menu. None means "not menu-eligible."
enum class FileClass : unsigned char {
    None = 0,
    Gcode,    // .gcode / .nc / .txt — _files_menu
    Bin,      // .bin             — _firmware_menu
    Cfg,      // .yaml            — _config_menu
};

// True if extension (lowercase, including leading dot, e.g.
// ".gcode") is in any of the three menu-eligible classes. Kept
// for callers that just want a yes/no.
bool is_extension_allowed(const char* extension_lower);

// Classify extension into its menu class. Returns FileClass::None
// if not menu-eligible.
FileClass classify_extension(const char* extension_lower);

// Walks the path's last segment and writes the lowercase extension
// (including leading dot) into `out` (size out_size). Returns
// false if no extension, hidden basename, or out is too small.
bool extract_lower_extension(const char* path, char* out,
                             size_t out_size);

// True if path equals dir_prefix exactly or starts with
// dir_prefix + "/". Mirrors the predicate used by
// Menu::remove_sd_subtree, exposed here so callers can reason
// about subtree membership without crossing into the Menu layer.
bool path_is_under(const char* path, const char* dir_prefix);

}  // namespace SDMenuEvents

// SD-event entry points. Implementations live in
// FluidNC/esp32/sdmmc.cpp / sdspi.cpp (one links per hardware
// target, mirroring sd_populate_files_menu).
//
// All three are no-ops on non-"/sd/" paths and on hosts without
// OLED. They are safe to call unconditionally from code paths
// that straddle SD and LocalFS — the gate lives here, callers do
// not duplicate it.
//
// Path arguments are fully-qualified (e.g. "/sd/foo.gcode"); the
// helpers strip the prefix internally before invoking Menu
// primitives, which operate on base_path-relative keys.
void sd_files_added(const char* full_path);
void sd_files_removed(const char* full_path, bool was_directory);
void sd_files_renamed(const char* old_full_path,
                      const char* new_full_path);
