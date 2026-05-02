// SDMenuEvents.cpp — pure helpers only.
//
// The SD-event entry points (sd_files_added / _removed / _renamed)
// live in the ESP-only TUs (FluidNC/esp32/sdmmc.cpp and
// FluidNC/esp32/sdspi.cpp) because they touch sd_is_mounted and
// the OLED config. The pieces here are the host-testable bits:
// path-form normalization, extension allowlist, subtree predicate.
//
// Issues: internal tracker.

#include "SDMenuEvents.h"

#include <cctype>
#include <cstring>

namespace SDMenuEvents {

const char* strip_sd_prefix(const char* full_path) {
    if (full_path == nullptr) return nullptr;
    // Strip "/sd" (NOT "/sd/") so the leading slash on the relative
    // form is preserved. The Menu cache stores entries in this form
    // because the populate function uses
    //     short_path = full_path.substr(strlen(base_path))
    // where base_path = "/sd" (no trailing slash). add_sd_file then
    // calls strrchr(path, '/') to find the basename, which crashes
    // if the path has no slash at all.
    //
    // Match "/sd" only when followed by '/' or end-of-string -- that
    // way "/sdcard/..." doesn't match.
    static const char kPrefix[]   = "/sd";
    static const size_t kLen      = sizeof(kPrefix) - 1;  // 3
    if (strncmp(full_path, kPrefix, kLen) != 0) return nullptr;
    char tail = full_path[kLen];
    if (tail != '/' && tail != '\0') return nullptr;
    return full_path + kLen;
}

// Mirrors the three extension classes used by sd_populate_files_menu
// in esp32/sdmmc.cpp. esp32/sdspi.cpp's populate only handles the
// gcode class (no .bin / .yaml branches), but classification here is
// global -- if a .bin is ever uploaded to a board running the sdspi
// backend, the helper still routes the menu entry correctly via
// _firmware_menu, which Menu owns regardless of backend.
static const char* const kGcodeExt[] = { ".gcode", ".nc", ".txt" };
static const char* const kBinExt[]   = { ".bin" };
static const char* const kCfgExt[]   = { ".yaml" };

FileClass classify_extension(const char* extension_lower) {
    if (extension_lower == nullptr) return FileClass::None;
    for (const char* e : kGcodeExt) {
        if (strcmp(extension_lower, e) == 0) return FileClass::Gcode;
    }
    for (const char* e : kBinExt) {
        if (strcmp(extension_lower, e) == 0) return FileClass::Bin;
    }
    for (const char* e : kCfgExt) {
        if (strcmp(extension_lower, e) == 0) return FileClass::Cfg;
    }
    return FileClass::None;
}

bool is_extension_allowed(const char* extension_lower) {
    return classify_extension(extension_lower) != FileClass::None;
}

bool extract_lower_extension(const char* path, char* out, size_t out_size) {
    if (path == nullptr || out == nullptr || out_size == 0) return false;
    const char* slash = strrchr(path, '/');
    const char* base = slash ? slash + 1 : path;
    const char* dot = strrchr(base, '.');
    if (dot == nullptr || dot == base) return false; // no ext or hidden
    size_t len = strlen(dot);
    if (len + 1 > out_size) return false;
    for (size_t i = 0; i <= len; ++i) {
        out[i] = static_cast<char>(std::tolower(
            static_cast<unsigned char>(dot[i])));
    }
    return true;
}

bool path_is_under(const char* path, const char* dir_prefix) {
    if (path == nullptr || dir_prefix == nullptr) return false;
    size_t plen = strlen(dir_prefix);
    if (strncmp(path, dir_prefix, plen) != 0) return false;
    char tail = path[plen];
    return tail == '\0' || tail == '/';
}

}  // namespace SDMenuEvents
