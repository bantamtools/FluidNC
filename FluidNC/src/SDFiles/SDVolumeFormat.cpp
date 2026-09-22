// FluidNC/src/SDFiles/SDVolumeFormat.cpp
// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the
// LICENSE file.

#include "SDVolumeFormat.h"

#include <cstddef>
#include <cstring>

namespace sdfiles {

namespace {

constexpr size_t   kSectorSize      = 512;
constexpr size_t   kOemNameOffset   = 3;
constexpr size_t   kPartTableOffset = 446;
constexpr size_t   kPartEntrySize   = 16;
constexpr int      kPartEntries     = 4;
constexpr uint8_t  kPartTypeGpt     = 0xEE;

bool hasBootSignature(const uint8_t* s) {
    return s[510] == 0x55 && s[511] == 0xAA;
}

bool oemNameIs(const uint8_t* s, const char* name) {
    return std::memcmp(s + kOemNameOffset, name, 8) == 0;
}

// True when the sector is a FAT12/16 or FAT32 volume boot record, identified by
// its filesystem type string.
bool isFatBootRecord(const uint8_t* s) {
    return std::memcmp(s + 54, "FAT", 3) == 0 || std::memcmp(s + 82, "FAT", 3) == 0;
}

// Identifies exFAT or NTFS from a volume boot record; Unrecognized otherwise.
SDVolumeFormat classifyBootRecord(const uint8_t* s) {
    if (oemNameIs(s, "EXFAT   ")) {
        return SDVolumeFormat::ExFat;
    }
    if (oemNameIs(s, "NTFS    ")) {
        return SDVolumeFormat::Ntfs;
    }
    return SDVolumeFormat::Unrecognized;
}

uint32_t readLe32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

}  // namespace

SDVolumeFormat classifyVolume(const SectorReader& read_sector) {
    uint8_t buf[kSectorSize];

    if (!read_sector(0, buf)) {
        return SDVolumeFormat::ReadError;
    }
    if (!hasBootSignature(buf)) {
        return SDVolumeFormat::Unformatted;
    }

    // An unpartitioned card carries its volume boot record at sector 0. The
    // filesystem signatures, not the leading jump opcode, tell it apart from an
    // MBR, since some MBR boot code also begins with a jump.
    SDVolumeFormat sector0 = classifyBootRecord(buf);
    if (sector0 != SDVolumeFormat::Unrecognized) {
        return sector0;
    }
    if (isFatBootRecord(buf)) {
        return SDVolumeFormat::Unrecognized;
    }

    // Otherwise sector 0 is an MBR. A protective entry marks a GPT disk; any
    // other nonempty entry is followed to its volume boot record.
    uint32_t first_lba = 0;
    for (int i = 0; i < kPartEntries; ++i) {
        const uint8_t* entry = buf + kPartTableOffset + i * kPartEntrySize;
        uint8_t        type  = entry[4];
        if (type == kPartTypeGpt) {
            return SDVolumeFormat::Gpt;
        }
        if (type != 0 && first_lba == 0) {
            first_lba = readLe32(entry + 8);
        }
    }
    if (first_lba == 0) {
        return SDVolumeFormat::Unrecognized;
    }

    if (!read_sector(first_lba, buf)) {
        return SDVolumeFormat::ReadError;
    }
    return classifyBootRecord(buf);
}

const char* unavailableMessage(SDVolumeFormat format) {
    switch (format) {
        case SDVolumeFormat::ExFat:
            return "exFAT not supported\nPlease reformat\nmicroSD as FAT32";
        case SDVolumeFormat::Ntfs:
            return "NTFS not supported\nPlease reformat\nmicroSD as FAT32";
        case SDVolumeFormat::Gpt:
            return "GPT not supported\nPlease reformat as\nFAT32 (MBR scheme)";
        case SDVolumeFormat::Unformatted:
            return "Unformatted microSD\nPlease format\nas FAT32";
        case SDVolumeFormat::Unrecognized:
            return "Unreadable microSD\nPlease reformat\nas FAT32";
        case SDVolumeFormat::ReadError:
            return "microSD read error\nPlease reinsert\nthe card";
        case SDVolumeFormat::Absent:
        default:
            return "No microSD Card";
    }
}

const char* formatName(SDVolumeFormat format) {
    switch (format) {
        case SDVolumeFormat::ExFat:
            return "exFAT";
        case SDVolumeFormat::Ntfs:
            return "NTFS";
        case SDVolumeFormat::Gpt:
            return "GPT-partitioned";
        case SDVolumeFormat::Unformatted:
            return "unformatted";
        case SDVolumeFormat::Unrecognized:
            return "unrecognized format";
        case SDVolumeFormat::ReadError:
            return "unreadable";
        case SDVolumeFormat::Absent:
        default:
            return "absent";
    }
}

}  // namespace sdfiles
