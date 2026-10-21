// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.
//
// TMC2209Stepper with a read() that accepts a valid reply whose CRC is 0x00
// (internal tracker). TMCStepper is pinned (platformio.ini), so the fix is an
// override rather than a patched library. Every register getter in the library
// reaches the chip through the virtual read(uint8_t), so IFCNT(), IOIN(),
// version() and the rest all take this path.
#pragma once

#include "TmcUartFrame.h"

#include <TMCStepper.h>

namespace MotorDrivers {
    class TrinamicUartStepper : public TMC2209Stepper {
    public:
        TrinamicUartStepper(Stream* port, float rsense, uint8_t addr) : TMC2209Stepper(port, rsense, addr) {}

    protected:
        // Same datagram, retries and timing as TMC2208Stepper::read(addr) in
        // TMCStepper 0.7.3. The only change is the acceptance test: a frame is
        // accepted when its CRC matches, including a CRC of 0x00. No reply at all
        // (_sendDatagram() returning 0) is still a failure.
        uint32_t read(uint8_t addr) override {
            constexpr uint8_t len        = 3;
            uint8_t           datagram[] = { TMC2208_SYNC, slave_address, uint8_t(addr | TMC_READ), 0x00 };
            datagram[len]                = calcCRC(datagram, len);
            uint64_t out                 = 0;

            for (uint8_t i = 0; i < max_retries; i++) {
                preReadCommunication();
                out = _sendDatagram(datagram, len, abort_window);
                postReadCommunication();

                delay(replyDelay);

                CRCerror = false;
                if (TmcUart::replyOk(out)) {
                    break;
                }
                CRCerror = true;
                out      = 0;
            }

            return static_cast<uint32_t>(out >> 8);
        }
    };
}
