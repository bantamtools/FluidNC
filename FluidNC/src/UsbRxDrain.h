// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license (see LICENSE).
#pragma once
#include <cstdint>

// : discard USB-Serial-JTAG OUT data stranded across HWCDC::begin().
//
// Bytes the host sends before Serial.begin() sit in the OUT endpoint buffer.
// HWCDC::begin() clears every interrupt status bit, including the SERIAL_OUT_RECV_PKT
// that announced them, so the ISR never reads the buffer, the host is NAKed forever,
// and the USB-JTAG block survives USB-triggered resets: RX is dead until power-on.
//
// The OUT buffer holds one packet, so a stranded packet is usually the head of a
// longer backlog the host is still retrying (a sender that was mid-stream when the
// chip reset, or a `cat file > port`). Discarding only the buffered packet would let
// that backlog reach the parser starting partway through a line. So when anything is
// stranded, keep discarding until the line has been quiet for quiet_us, bounded by
// cap_us against a host that never stops. A normal boot (nothing stranded) returns
// after one check with no delay.
//
// Each pass clears RECV_PKT FIRST, then checks for data. A packet landing after the
// final check therefore leaves RECV_PKT raw-pending and the ISR services it once the
// caller re-enables the interrupt; a packet landing between the clear and the check
// is seen and read here. The caller must keep the ISR from reading the FIFO meanwhile
// (FluidNC masks the RECV_PKT interrupt enable). The register operations, clock and
// sleep are injected so this is host-testable. Returns the number of bytes discarded.
template <class ClearRecvPkt, class DataAvail, class ReadPacket, class NowUs, class Sleep>
uint32_t drain_stranded_rx(ClearRecvPkt clearRecvPkt,
                           DataAvail    dataAvail,
                           ReadPacket   readPacket,
                           NowUs        nowUs,
                           Sleep        sleepBriefly,
                           uint32_t     quiet_us,
                           uint32_t     cap_us) {
    clearRecvPkt();
    if (!dataAvail()) {
        return 0;
    }
    uint32_t       drained   = readPacket();
    const uint32_t start     = nowUs();
    uint32_t       last_data = start;
    for (;;) {
        clearRecvPkt();
        if (dataAvail()) {
            drained += readPacket();
            last_data = nowUs();
        } else if (uint32_t(nowUs() - last_data) >= quiet_us) {
            break;
        } else {
            sleepBriefly();
        }
        if (uint32_t(nowUs() - start) >= cap_us) {
            break;
        }
    }
    return drained;
}
