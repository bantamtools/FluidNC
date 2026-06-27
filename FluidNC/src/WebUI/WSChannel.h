// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Config.h"  // ENABLE_*

#include <cstdint>
#include <cstring>
#include <list>
#include <map>
#include <atomic>

class WebSocketsServer;

#ifndef ENABLE_WIFI
#    if 0
namespace WebUI {
    class WSChannel {
    public:
        WSChannel(WebSocketsServer* server, uint8_t clientNum);
        int    read() { return -1; }
        size_t write(const uint8_t* buffer, size_t size) { return 0; }
    };
}
#    endif
#else

#    include "../Channel.h"
#    include "TxRing.h"

namespace WebUI {
    class WSChannel : public Channel {
    public:
        WSChannel(WebSocketsServer* server, uint8_t clientNum);

        size_t write(uint8_t c);
        size_t write(const uint8_t* buffer, size_t size);

        bool sendTXT(std::string& s);

        inline size_t write(const char* s) { return write((uint8_t*)s, ::strlen(s)); }
        inline size_t write(unsigned long n) { return write((uint8_t)n); }
        inline size_t write(long n) { return write((uint8_t)n); }
        inline size_t write(unsigned int n) { return write((uint8_t)n); }
        inline size_t write(int n) { return write((uint8_t)n); }

        bool push(const uint8_t* data, size_t length);
        bool push(std::string& s);
        void pushRT(char ch);

        void flush(void) override {}

        int id() { return _clientNum; }

        // ( Stage 2 / M4) _queue.size() is read on the poller while the wifi_task
        // pushes -> lock it (the override taken inside WSChannel, per the review; the
        // base methods are never called for a WS channel).
        int rx_buffer_available() override {
            lockInput();
            int n = int(_queue.size());
            unlockInput();
            return std::max(0, 256 - n);
        }

        operator bool() const;

        ~WSChannel();

        int read() override;
        int available() override {
            lockInput();
            int n = int(_queue.size());
            unlockInput();
            return n + (_rtchar.load(std::memory_order_acquire) > -1);
        }

        void autoReport() override;

        // ( Phase-2 Stage 1) Drain the TX ring -> sendBIN. Runs on the output
        // consumer task (output_loop now; wifi_task after Stage 2).
        bool drainTx(uint8_t* buf, size_t cap, bool& committedAny) override;

        // ( Stage 1, minor) Let WSChannels::removeChannel() mark the channel
        // dead so an in-flight drain skips the socket op on a torn-down channel.
        void markDead() { _dead.store(true, std::memory_order_release); }

        uint32_t txDropped() const { return _tx.dropped(); }

    private:
        // ( Stage 2 / M4) Guards _queue against the wifi_task push vs the poller
        // front/pop/size race. portMUX (not std::mutex): the poller (tier 2/3) must spin
        // briefly, never block, on the wifi_task (tier 4). Critical sections are short
        // structural _queue ops only; the push is frame-capped so the locked window stays
        // bounded on core 0 (steppers are core 1, unaffected).
        portMUX_TYPE _queueMux = portMUX_INITIALIZER_UNLOCKED;
        void         lockInput() override { portENTER_CRITICAL(&_queueMux); }
        void         unlockInput() override { portEXIT_CRITICAL(&_queueMux); }

        //  Atomic: set on the output/poller write paths (sendBIN/sendTXT fail),
        // read on the poller; a plain bool races across those tasks.
        std::atomic<bool> _dead { false };

        WebSocketsServer* _server;
        uint8_t           _clientNum;

        std::string _output_line;

        // ( Phase-2 Stage 1) TX ring: output_loop appends complete lines
        // (one record == one sendBIN frame); the drain sends them. 512 B (MAX_RECORD
        // 510) covers any single status/console line (JSON is pre-split into short
        // lines by JSONencoder::line()). Sized minimally on purpose: the drain runs
        // every output_loop iteration, so a healthy ring holds only ~1 record, and a
        // smaller per-channel footprint protects the no-PSRAM heap floor under
        // connect/disconnect churn (the  OOM class). Bench: a 1024 B ring dropped
        // min-free to 448 B under an aggressive WS churn storm vs 2.59 KB on the
        // no-ring baseline; 512 B roughly halves that amplification.
        TxRing<512> _tx;

        // Instead of queueing realtime characters, we put them here
        // so they can be processed immediately during operations like
        // homing where GCode handling is blocked.
        // ( Stage-0/B4) atomic: pushRT (wifi_task after the move) vs read() (poller).
        std::atomic<int> _rtchar { -1 };
    };

    class WSChannels {
    private:
        static std::map<uint8_t, WSChannel*> _wsChannels;
        static std::list<WSChannel*>         _webWsChannels;

        static WSChannel* _lastWSChannel;
        static WSChannel* getWSChannel(int pageid);

    public:
        static void removeChannel(WSChannel* channel);
        static void removeChannel(uint8_t num);
        static void removeAllChannels();

        static bool runGCode(int pageid, std::string cmd);
        static bool sendError(int pageid, std::string error);
        static void sendPing();
        static void handleEvent(WebSocketsServer* server, uint8_t num, uint8_t type, uint8_t* payload, size_t length);
    };
}

#endif
