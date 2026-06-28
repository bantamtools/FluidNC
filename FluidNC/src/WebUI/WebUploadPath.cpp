// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.
//
// Dependency-free path normalization for web upload targets.
// No ESP or Arduino headers — builds in the native host test harness AND on ESP32.

#include "WebUploadPath.h"

#include <string>

std::string normalizeWebUploadPath(std::string path) {
    // Step 1: collapse "//" → "/" (loop until stable)
    std::string::size_type pos;
    while ((pos = path.find("//")) != std::string::npos) {
        path.erase(pos, 1);
    }

    // Step 2: strip trailing "/"
    if (!path.empty() && path.back() == '/') {
        path.pop_back();
    }

    // Step 3: strip leading "/"
    if (!path.empty() && path.front() == '/') {
        path.erase(0, 1);
    }

    return path;
}
