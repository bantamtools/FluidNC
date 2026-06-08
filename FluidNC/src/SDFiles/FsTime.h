// FluidNC/src/SDFiles/FsTime.h
// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the
// LICENSE file.
//
// Converts a std::filesystem::file_time_type to a uint32_t sort key for SD
// directory entries.

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>

namespace sdfiles {

// Returns t as a monotonic sort key in seconds relative to the filesystem
// clock's epoch. The value is meaningful only for ordering entries against
// each other — all entries share the same clock, so differences are valid
// relative durations. It is NOT a Unix wall-clock timestamp. Pre-epoch
// timestamps (negative offset) are not ordered relative to post-epoch values,
// as the unsigned cast wraps them to high values.
inline uint32_t toMtimeSeconds(std::filesystem::file_time_type t) {
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            t.time_since_epoch()).count());
}

}  // namespace sdfiles
