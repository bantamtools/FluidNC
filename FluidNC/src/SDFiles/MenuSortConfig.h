// FluidNC/src/SDFiles/MenuSortConfig.h
// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the
// LICENSE file.
//
// Compile-time selector for the SD file-menu DISPLAY sort order.
//
// The default is BT_SD_MENU_SORT_FOLLOW: the menu follows the runtime "Next
// File Ordering" setting (Oldest / Newest / A->Z / Z->A), so changing that
// setting changes the on-screen file order. The other values pin the menu to
// one fixed order regardless of the setting. A build may override the default
// by defining BT_SD_MENU_SORT before this header is reached (e.g. a -D flag);
// the value selected here applies only when nothing overrides it.
//
// The date orders (FOLLOW resolving to Oldest/Newest, or the fixed NEWEST /
// OLDEST values) sort on file mtime, populated by the SD scan and the
// incremental file-event handlers. The name orders need no mtime.

#pragma once

#include "src/SDFiles/SDFileTable.h"
#include "src/NextFileOrdering.h"

// Named constants for BT_SD_MENU_SORT.
#define BT_SD_MENU_SORT_FOLLOW    0  // use the NextFileOrdering runtime setting
#define BT_SD_MENU_SORT_NAME_ASC  1  // A->Z
#define BT_SD_MENU_SORT_NEWEST    2  // newest -> oldest
#define BT_SD_MENU_SORT_NAME_DESC 3  // Z->A
#define BT_SD_MENU_SORT_OLDEST    4  // oldest -> newest

// Default: follow the runtime "Next File Ordering" setting.
#ifndef BT_SD_MENU_SORT
#define BT_SD_MENU_SORT BT_SD_MENU_SORT_FOLLOW
#endif

namespace sdfiles {

// Maps a NextFileOrdering::Order value to its SortMode equivalent.
inline SortMode mapOrderToSortMode(NextFileOrdering::Order o) {
    switch (o) {
        case NextFileOrdering::Order::Oldest: return SortMode::Oldest;
        case NextFileOrdering::Order::Newest: return SortMode::Newest;
        case NextFileOrdering::Order::A_to_Z: return SortMode::NameAsc;
        case NextFileOrdering::Order::Z_to_A: return SortMode::NameDesc;
    }
    // Defensive fallback: unreachable for valid Order enumerators; satisfies
    // -Wreturn-type and defines behavior if a future Order value is added.
    return SortMode::NameAsc;
}

// Returns the SortMode indicated by macroValue. BT_SD_MENU_SORT_FOLLOW defers
// to settingOrder; all other values select a fixed mode regardless of the setting.
// An unrecognized macroValue returns NameAsc.
inline SortMode resolveMenuSortMode(int macroValue,
                                    NextFileOrdering::Order settingOrder) {
    switch (macroValue) {
        case BT_SD_MENU_SORT_FOLLOW:    return mapOrderToSortMode(settingOrder);
        case BT_SD_MENU_SORT_NAME_ASC:  return SortMode::NameAsc;
        case BT_SD_MENU_SORT_NEWEST:    return SortMode::Newest;
        case BT_SD_MENU_SORT_NAME_DESC: return SortMode::NameDesc;
        case BT_SD_MENU_SORT_OLDEST:    return SortMode::Oldest;
        default: return SortMode::NameAsc;  // out-of-range macro value
    }
}

// Returns the SortMode for the current build and runtime configuration.
// Defined in MenuSortConfig.cpp (device build only; excluded from host tests).
SortMode menuSortMode();

}  // namespace sdfiles
