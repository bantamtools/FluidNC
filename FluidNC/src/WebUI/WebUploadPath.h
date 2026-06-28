// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once
#include <string>

// Dependency-free path normalization for web upload targets.
// No ESP or Arduino headers — safe to build in the native [env:tests] harness.
//
// Rules (applied in order):
//   1. Collapse all "//" sequences to "/"
//   2. Strip a trailing "/" (if non-empty after step 1)
//   3. Strip a leading "/" (if non-empty after step 2)
//   4. Empty input → empty output (no UB)
//
// This fixes the operator-precedence bug in the original inline normalization:
//   `if (path.length() & path[0] == '/')` — `==` binds tighter than `&`,
//   so the condition tests the low bit of the length rather than emptiness,
//   and also accesses path[0] unconditionally (UB on empty path).

std::string normalizeWebUploadPath(std::string path);
