// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.
//
// TMC2208/TMC2209 UART reply-frame CRC check. Pure, so it is natively testable.
//
// TMCStepper 0.7.3 TMC2208Stepper::read() rejects a reply when
// `crc != frame_crc || crc == 0`. A computed CRC of 0x00 is a valid CRC, so a
// register value whose reply happens to CRC to zero can never be read: every
// retry sends the same request, gets the same correct reply and rejects it
// (internal tracker; IFCNT 174 on the bench HandWriter). The empty-reply case
// the `crc == 0` test caught as a side effect is _sendDatagram() returning 0,
// which TrinamicUartStepper::read() rejects explicitly instead.
#pragma once

#include <cstdint>

namespace MotorDrivers {
    namespace TmcUart {
        // TMCStepper::calcCRC: CRC-8, polynomial 0x07, each byte fed LSB first.
        inline uint8_t crc(const uint8_t* d, uint8_t len) {
            uint8_t c = 0;
            for (uint8_t i = 0; i < len; i++) {
                uint8_t b = d[i];
                for (uint8_t j = 0; j < 8; j++) {
                    if ((c >> 7) ^ (b & 0x01)) {
                        c = uint8_t((c << 1) ^ 0x07);
                    } else {
                        c = uint8_t(c << 1);
                    }
                    b >>= 1;
                }
            }
            return c;
        }

        // `out` is the 8-byte reply as _sendDatagram() packs it (byte 0 in bits 63..56).
        // An all-zero `out` means no reply arrived and is never a valid frame.
        inline bool replyOk(uint64_t out) {
            if (out == 0) {
                return false;
            }
            uint8_t f[7];
            for (uint8_t i = 0; i < 7; i++) {
                f[i] = uint8_t(out >> (56 - 8 * i));
            }
            return crc(f, 7) == uint8_t(out);
        }
    }
}
