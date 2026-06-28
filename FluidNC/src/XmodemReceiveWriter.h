// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

// Pure, dependency-free write-accounting for the XModem receive path.
// Holds the most-recently-received packet so the trailing Ctrl-Z padding can
// be stripped from ONLY the final packet (XModem has no byte-exact length),
// and — critically — checks every sink write so a short write (SD full / write
// error) aborts the transfer instead of being silently reported as success.
// No ESP/Arduino dependencies, so it is unit-tested natively .
class XmodemReceiveWriter {
public:
    // Sink writes len bytes and returns the count actually written
    // (Print::write / fwrite semantics). A return < len is a write failure.
    using Sink = std::function<size_t(const uint8_t* buf, size_t len)>;

    explicit XmodemReceiveWriter(Sink sink) : _sink(std::move(sink)) {}

    // Flush the previously-held packet in full (if any), then hold this one.
    // Returns false if the flushed write was short (write failure).
    // Abort contract: on false, _heldLen is left as-is; the caller MUST abort
    // the transfer and not call again — the instance is destroyed at end of xmodemReceive.
    bool acceptPacket(const uint8_t* buf, size_t len);

    // Flush the final held packet with trailing Ctrl-Z (0x1A) stripped.
    // Returns false if the write was short. Safe to call with nothing held.
    // Abort contract: same as acceptPacket — caller must abort on false.
    bool flushFinal();

    // Total bytes successfully written so far.
    size_t totalWritten() const { return _total; }

private:
    static constexpr uint8_t CTRLZ      = 0x1A;
    // XModem bufsz is always 128 (SOH) or 1024 (STX); the clamp in acceptPacket
    // is defensive belt-and-suspenders — standard 128-byte packets also fit under this cap.
    static constexpr size_t  MAX_PACKET = 1024;  // XModem-1K

    bool writeAll(const uint8_t* buf, size_t len);

    Sink    _sink;
    uint8_t _held[MAX_PACKET];
    size_t  _heldLen = 0;
    size_t  _total   = 0;
};
