// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.
//
// Line splitting for StartupLog::dump(), walking the log in place. Pure, so it is
// natively testable.
//
// dump() used to build a std::istringstream from the log, which copies the whole
// log (about 12 kB on a HandWriter) into a second contiguous heap block. With a
// fragmented heap that allocation threw std::bad_alloc and aborted the firmware
// on `$SS` (internal tracker). This visits each line without copying the log.
#pragma once

#include <cstddef>
#include <string>

// Calls f(const char* start, size_t length) for each '\n'-terminated line of `s`,
// excluding the '\n', with the same lines std::getline would produce: a final
// line without a trailing '\n' is visited, a trailing '\n' does not produce an
// extra empty line, and empty lines in the middle are visited.
template <typename F>
void forEachStartupLogLine(const std::string& s, F f) {
    size_t start = 0;
    while (start < s.size()) {
        size_t nl = s.find('\n', start);
        if (nl == std::string::npos) {
            f(s.data() + start, s.size() - start);
            return;
        }
        f(s.data() + start, nl - start);
        start = nl + 1;
    }
}
