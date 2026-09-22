// FluidNC/src/SDFiles/SDVolumeFormat.h
// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the
// LICENSE file.
//
// Identifies why a microSD card that responds to commands could not be mounted,
// by inspecting its boot sector and partition table. The firmware's FatFs build
// reads only FAT volumes on MBR-partitioned or unpartitioned cards; exFAT, NTFS,
// and GPT-partitioned cards fail to mount and are reported here by name so the
// user can be told how to reformat.

#pragma once

#include <cstdint>
#include <functional>

namespace sdfiles {

enum class SDVolumeFormat {
    Absent,        // No card, or the card did not respond.
    ExFat,         // exFAT volume.
    Ntfs,          // NTFS volume.
    Gpt,           // GUID partition table (protective MBR).
    Unformatted,   // Sector 0 lacks the 0x55AA boot signature.
    Unrecognized,  // Signed sector 0, but no volume type identified here.
    ReadError,     // A sector needed for identification could not be read.
};

// Reads one 512-byte sector at lba into buf. Returns false on failure.
using SectorReader = std::function<bool(uint32_t lba, uint8_t* buf)>;

// Classifies the card by reading sector 0 and, for an MBR, the boot sector of the
// first partition.
SDVolumeFormat classifyVolume(const SectorReader& read_sector);

// Returns a short OLED popup message for a card that is absent or unmountable.
// Each message fits three popup lines.
const char* unavailableMessage(SDVolumeFormat format);

// Returns a short lowercase name for the format, for log messages.
const char* formatName(SDVolumeFormat format);

}  // namespace sdfiles
