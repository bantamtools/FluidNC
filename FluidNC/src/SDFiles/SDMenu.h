// FluidNC/src/SDFiles/SDMenu.h
// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the
// LICENSE file.
//
// Glue helpers between FluidNC's path-based SD layer and SDFileTable's id-based
// arena. Pure C++; no Arduino/ESP dependencies; host-testable in isolation.

#pragma once

#include "src/SDFiles/SDFileTable.h"

namespace SDMenu {

// Resolves a base-relative SD path ("/dir/sub/basename"; leading '/', no "/sd")
// to the matching arena EntryId by walking components via findChild. A leading
// checkmark prefix on the basename is stripped before matching (the arena stores
// canonical names; on-disk paths may carry the U+2713 prefix). A trailing '/'
// is tolerated. Returns kInvalidEntry for null/empty input, or if any component
// does not match a live child.
sdfiles::EntryId resolvePath(const sdfiles::SDFileTable& t, const char* path);

}  // namespace SDMenu
