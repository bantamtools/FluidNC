// FluidNC/src/SDFiles/MenuSortConfig.cpp
// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the
// LICENSE file.
//
// Device-only definition of menuSortMode(). This translation unit is compiled
// into the device firmware (auto-included by the +<src/> filter) but is
// excluded from the host-test build, keeping the NVS dependency out of tests.

#include "src/SDFiles/MenuSortConfig.h"
#include "src/NextFileOrdering.h"

namespace sdfiles {

SortMode menuSortMode() {
    return resolveMenuSortMode(BT_SD_MENU_SORT, NextFileOrdering::get());
}

}  // namespace sdfiles
