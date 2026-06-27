// Copyright 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// #include "../Machine/MachineConfig.h"
#include "TelnetClient.h"
#include "TelnetServer.h"

#ifdef ENABLE_WIFI

#    include "WifiServices.h"

#    include <WiFi.h>
#    include "WifiConfig.h"  //  WiFiConfig::staTxSuspect()
#    include "../Serial.h"   //  g_net_tx_dirty / g_net_tx_dropped

// ( r4) [HB] attribution: set while parked in a network socket send (2=telnet).
// Defined in Protocol.cpp; declared here to avoid pulling in all of Protocol.h.
extern volatile uint8_t g_out_netwrite;

namespace WebUI {
    TelnetClient::TelnetClient(WiFiClient* wifiClient) : Channel("telnet"), _wifiClient(wifiClient) {}

    void TelnetClient::handle() {}

    void TelnetClient::closeOnDisconnect() {
        // ( Stage-0/M2) Called from read() (poller) AND write() (output task; the
        // wifi_task drain after the move). CAS the disconnected flag so EXACTLY ONE
        // caller wins the false->true transition and enqueues; the queue push is under
        // the lock. Prevents a double-enqueue -> double kill() under disconnect churn.
        if (_disconnected.load(std::memory_order_acquire) || _wifiClient->connected()) {
            return;
        }
        bool expected = false;
        if (_disconnected.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            std::lock_guard<std::mutex> lock(telnetServer._disconnectedMutex);
            telnetServer._disconnected.push(this);
        }
    }

    void TelnetClient::flushRx() { Channel::flushRx(); }

    size_t TelnetClient::write(uint8_t data) { return write(&data, 1); }

    size_t TelnetClient::write(const uint8_t* buffer, size_t length) {
        //  While STA is down the socket is half-open. Gate before enqueueing
        // so we don't fill the ring with undeliverable bytes (mirrors today's drop).
        if (WiFiConfig::staTxSuspect()) {
            return length;  // pretend-sent; the line is dropped while the link is down
        }
        // Replace \n with \r\n, appending each expanded chunk to the TX ring.
        size_t  rem      = length;
        uint8_t lastchar = '\0';
        size_t  j        = 0;
        while (rem) {
            const int bufsize = 128;
            uint8_t   modbuf[bufsize];
            // bufsize-1 in case the last character is \n
            size_t k = 0;
            while (rem && k < (bufsize - 1)) {
                uint8_t c = buffer[j++];
                if (c == '\n' && lastchar != '\r') {
                    modbuf[k++] = '\r';
                }
                lastchar    = c;
                modbuf[k++] = c;
                --rem;
            }
            if (k) {
                // ( Phase-2 Stage 1) Append the \r\n-expanded chunk to the TX
                // ring instead of the blocking WiFiClient::write() (which parks
                // ~10s on a half-open socket and would wedge the core-0 comms
                // tasks). The drain does the socket send. Telnet is a byte stream;
                // chunks drain in FIFO order, so overflow tail-truncates a line
                // (matches today) and never holes mid-line.
                if (_tx.push(modbuf, (uint32_t)k)) {
                    g_net_tx_dirty.store(true, std::memory_order_release);
                } else {
                    g_net_tx_dropped.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        return length;
    }

    // ( Phase-2 Stage 1) Drain the TX ring -> WiFiClient::write. Runs on the
    // single output consumer task (output_loop now; wifi_task after Stage 2).
    // Peek/commit: the record is advanced past ONLY after a successful send, so a
    // back-pressured socket leaves it queued in FIFO order (no loss, no mid-line hole).
    bool TelnetClient::drainTx(uint8_t* buf, size_t cap, bool& committedAny) {
        int      drained = 0;
        uint32_t n;
        while (drained < TX_DRAIN_MAX_RECORDS) {
            if (!_tx.peek(buf, (uint32_t)cap, n)) {
                return false;  // ring empty
            }
            if (_disconnected.load(std::memory_order_acquire) || WiFiConfig::staTxSuspect()) {
                return true;  // not deliverable now; leave queued
            }
            // Non-blocking bound: if the socket can't accept right now, STOP
            // draining this channel and leave the record at the head in FIFO order.
            if (_wifiClient->canWrite(0) <= 0) {
                return true;
            }
            g_out_netwrite = 2;  //  [HB] attribution: parked in a telnet socket send
            auto nWritten  = _wifiClient->write(buf, n);
            g_out_netwrite = 0;
            if (nWritten == 0) {
                closeOnDisconnect();
                return false;
            }
            _tx.commit(n);
            committedAny = true;
            ++drained;
        }
        return true;  // bound hit; more may remain
    }

    int TelnetClient::peek(void) { return _wifiClient->peek(); }

    int TelnetClient::available() { return _wifiClient->available(); }

    int TelnetClient::rx_buffer_available() { return WIFI_CLIENT_READ_BUFFER_SIZE - available(); }

    int TelnetClient::read(void) {
        if (_disconnected.load(std::memory_order_acquire)) {
            return -1;
        }
        auto ret = _wifiClient->read();
        if (ret < 0) {
            // calling _wifiClient->connected() is expensive when the client is
            // connected because it calls recv() to double check, so we check
            // infrequently, only after quite a few reads have returned no data.
            // ( Stage-0/M2) the counter is poller-only; closeOnDisconnect sets
            // the atomic disconnected flag.
            if (++_disconnectCounter >= DISCONNECT_CHECK_COUNTS) {
                _disconnectCounter = 0;
                closeOnDisconnect();
            }
        } else {
            // Reset the counter if we have data
            _disconnectCounter = 0;
        }
        return ret;
    }

    TelnetClient::~TelnetClient() { delete _wifiClient; }
}

#endif
