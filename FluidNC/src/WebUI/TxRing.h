// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

// ( Phase-2 Stage 1) Per-network-channel TX ring buffer.
//
// This is the cross-task handoff between the OUTPUT PRODUCER (output_loop today;
// the dedicated wifi_task after Stage 2) and the DRAIN that performs the actual
// blocking socket send (sendBIN / WiFiClient::write). The producer only ever
// APPENDS to the ring (in-memory, microseconds, never blocks); the drain pops a
// record, releases the lock, and does the socket I/O. This is what lets the
// blocking network write be moved off the real-time tasks in Stage 2.
//
// LOAD-BEARING INVARIANT (design B1): the portMUX critical section guards ONLY
// the index math + memcpy into/out of the buffer. The socket send happens AFTER
// the lock is released (peek -> unlock -> send -> commit). A lock is NEVER held
// across a send/select.
//
// MULTI-PRODUCER / SINGLE-CONSUMER, made safe by the portMUX around ALL index math
// + memcpy (push/peek/commit). Do NOT "optimize" to a lock-free SPSC ring: post-Stage-2
// there are several producers (output_loop broadcast, the poller's autoReport enqueue
// path, direct command output) plus the boot-window direct-println fallback, so removing
// the portMUX would corrupt the ring and the no-PSRAM heap. The single consumer is the
// output drain task (output_loop in Stage 1; the wifi_task after Stage 2).
//
// Records are length-prefixed: [uint16 len][len bytes]. Free-running uint32
// head/tail (masked to the buffer on access) make FULL vs EMPTY unambiguous
// (used = head - tail; free = CAP - used) and survive index wrap for any
// CAP << 2^32. A record may wrap the physical buffer end (handled by a two-part
// masked memcpy) but is always read back whole.
//
// Overflow / oversized policy: push() drops (and counts) the WHOLE record if it
// does not fit or exceeds MAX_RECORD. Never a partial append (a half WS frame
// would desync the client). WiFi is the lowest-priority tier, so dropping its
// output to protect plotting/OLED/USB is correct.

#include <cstdint>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>  // portMUX_TYPE, portENTER_CRITICAL

namespace WebUI {

    // Max records a single channel drains per pass, so one chatty channel cannot
    // monopolize the output consumer loop. Remaining records drain next pass.
    static constexpr int TX_DRAIN_MAX_RECORDS = 8;

    // ( Stage 2b, review D2) Size of the single shared scratch buffer the drain
    // (AllChannels::drainTxRings) copies each record into before sending. EVERY ring's
    // CAP must be <= this (asserted below), so peek() can always fit a stored record —
    // else full-size records would be silently dropped. Bump in lockstep with any ring
    // CAP increase. Both current rings are TxRing<512>.
    static constexpr uint32_t TX_DRAIN_BUF = 512;

    template <uint32_t CAP>
    class TxRing {
        static_assert((CAP & (CAP - 1)) == 0, "TxRing CAP must be a power of two");
        static_assert(CAP <= TX_DRAIN_BUF, "TxRing CAP exceeds the shared drain buffer (bump TX_DRAIN_BUF)");
        static constexpr uint32_t MASK = CAP - 1;

        uint8_t      _buf[CAP];
        uint32_t     _head    = 0;  // producer appends here (free-running)
        uint32_t     _tail    = 0;  // consumer reads here   (free-running)
        uint32_t     _dropped = 0;  // records dropped (ring full / oversized)
        portMUX_TYPE _mux     = portMUX_INITIALIZER_UNLOCKED;

        // Copy len bytes FROM src INTO the ring at free-running offset `at`,
        // wrapping the physical buffer. Caller holds _mux.
        void putBytes(uint32_t at, const uint8_t* src, uint32_t len) {
            uint32_t first = at & MASK;
            uint32_t n1    = (first + len <= CAP) ? len : (CAP - first);
            memcpy(_buf + first, src, n1);
            if (n1 < len) {
                memcpy(_buf, src + n1, len - n1);
            }
        }
        // Copy len bytes FROM the ring at offset `at` INTO dst, wrapping. Caller holds _mux.
        void getBytes(uint32_t at, uint8_t* dst, uint32_t len) const {
            uint32_t first = at & MASK;
            uint32_t n1    = (first + len <= CAP) ? len : (CAP - first);
            memcpy(dst, _buf + first, n1);
            if (n1 < len) {
                memcpy(dst + n1, _buf, len - n1);
            }
        }

    public:
        // Largest record push() will accept. The drain-local buffer MUST be at
        // least this big, so peek() can always fit a stored record (closes the
        // "oversized record wedges the tail" trap, review B-1).
        static constexpr uint32_t MAX_RECORD = CAP - 2;

        TxRing() = default;

        // PRODUCER. Append [len][data] atomically, or drop the whole record and
        // return false. Never partial.
        bool push(const uint8_t* data, uint32_t len) {
            if (len == 0 || len > MAX_RECORD) {
                portENTER_CRITICAL(&_mux);
                ++_dropped;
                portEXIT_CRITICAL(&_mux);
                return false;
            }
            bool     ok   = false;
            uint32_t need = len + 2;
            portENTER_CRITICAL(&_mux);
            uint32_t used = _head - _tail;
            if (CAP - used >= need) {
                uint8_t hdr[2] = { uint8_t(len & 0xff), uint8_t((len >> 8) & 0xff) };
                putBytes(_head, hdr, 2);
                putBytes(_head + 2, data, len);
                _head += need;
                ok = true;
            } else {
                ++_dropped;
            }
            portEXIT_CRITICAL(&_mux);
            return ok;
        }

        // CONSUMER peek. Copy the head record into out[cap]; return its length in
        // outLen. Returns false ONLY when the ring is empty. A record larger than
        // cap cannot occur when cap >= MAX_RECORD, but is skipped (tail advanced,
        // counted) rather than sticking forever -- belt-and-suspenders (review B-1).
        bool peek(uint8_t* out, uint32_t cap, uint32_t& outLen) {
            bool got = false;
            portENTER_CRITICAL(&_mux);
            while (_head != _tail) {
                uint8_t hdr[2];
                getBytes(_tail, hdr, 2);
                uint32_t len = uint32_t(hdr[0]) | (uint32_t(hdr[1]) << 8);
                if (len > cap) {
                    _tail += len + 2;  // unreachable in practice; never wedge
                    ++_dropped;
                    continue;
                }
                getBytes(_tail + 2, out, len);
                outLen = len;
                got    = true;
                break;
            }
            portEXIT_CRITICAL(&_mux);
            return got;
        }

        // CONSUMER commit. Advance the tail past the just-peeked record. Call ONLY
        // after that record was successfully sent. On back-pressure, do NOT commit
        // -- the record stays at the head of the ring in FIFO order for the next
        // drain pass (no loss, no middle-splice).
        void commit(uint32_t len) {
            portENTER_CRITICAL(&_mux);
            _tail += len + 2;
            portEXIT_CRITICAL(&_mux);
        }

        uint32_t dropped() const { return _dropped; }
    };

}
