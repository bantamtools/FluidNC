// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

// Dependency-free SHA-256 (no mbedTLS) so it builds in the native [env:tests]
// host harness AND on the ESP32 device, and the host test exercises the exact
// code the $SD/Checksum (ESP222) command uses to hash uploaded G-code files.

// Streaming API — used by the file-hashing command (chunked read + yield).
struct Sha256Ctx {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  block[64];
    size_t   blocklen;
};
void sha256_init(Sha256Ctx& ctx);
void sha256_update(Sha256Ctx& ctx, const uint8_t* data, size_t len);
void sha256_final(Sha256Ctx& ctx, uint8_t out[32]);

// 32-byte digest -> 64-char UPPERCASE hex (no quotes).
std::string sha256ToHexUpper(const uint8_t digest[32]);

// One-shot convenience: SHA-256 of [data, len) -> 64-char UPPERCASE hex.
std::string sha256HexUpper(const uint8_t* data, size_t len);
