// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license (see LICENSE).
#include "PendingFileRun.h"

namespace PendingFileRun {
static std::string s_path;
void        set(const std::string& path) { s_path = path; }
bool        has() { return !s_path.empty(); }
std::string take() { std::string p = s_path; s_path.clear(); return p; }
void        clear() { s_path.clear(); }
}
