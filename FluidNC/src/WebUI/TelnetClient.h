// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Config.h"  // ENABLE_*
#include "../Channel.h"
#include <atomic>

#ifdef ENABLE_WIFI
#    include <WiFi.h>
#    include "TxRing.h"

namespace WebUI {
    class TelnetClient : public Channel {
        WiFiClient* _wifiClient;

        // The default value of the rx buffer in WiFiClient.cpp is 1436 which is
        // related to the network frame size minus TCP/IP header sizes.
        // The WiFiClient API has no way to override or query it.
        // We use a smaller value for safety.  There is little advantage
        // to sending too many GCode lines at once, especially since the
        // common serial communication case is typically limited to 128 bytes.
        static const int WIFI_CLIENT_READ_BUFFER_SIZE = 1200;

        static const int DISCONNECT_CHECK_COUNTS = 1000;

        // ( Stage-0/M2) _state was dual-purpose (a read-side throttle counter AND
        // the -1 disconnected flag); the counter's ++ could clobber the flag once the
        // two sides ran on different tasks. Split: the counter is poller-only (read());
        // the disconnected flag is atomic + CAS-gated so exactly one caller enqueues.
        int              _disconnectCounter = 0;
        std::atomic<bool> _disconnected { false };

        // ( Phase-2 Stage 1) TX ring: write() appends the \r\n-expanded bytes
        // as <=128 B records; the drain does the actual WiFiClient::write. 512 B
        // holds several chunks; telnet is a byte stream so records need no framing.
        TxRing<512> _tx;

    public:
        TelnetClient(WiFiClient* wifiClient);

        int    rx_buffer_available() override;
        size_t write(uint8_t data) override;
        size_t write(const uint8_t* buffer, size_t size) override;
        int    read(void) override;
        int    peek(void) override;
        int    available() override;
        void   flush() override {}
        void   flushRx() override;

        bool drainTx(uint8_t* buf, size_t cap, bool& committedAny) override;

        void closeOnDisconnect();

        uint32_t txDropped() const { return _tx.dropped(); }

        void handle() override;

        ~TelnetClient();
    };
}
#endif
