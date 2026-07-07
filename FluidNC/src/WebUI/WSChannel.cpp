// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "WSChannel.h"
#include "WifiConfig.h"     //  WiFiConfig::staTxSuspect()
#include <esp_heap_caps.h>  //  heap attribution at the WS-drop instant
#include <new>              // ( Stage 1) std::nothrow for channel allocation
#include "../GCode.h"       //  reemit_pause_instruction()

#ifdef ENABLE_WIFI
#    include "WebServer.h"
#    include <WebSocketsServer.h>
#    include <WiFi.h>

#    include "../Serial.h"  // is_realtime_command, g_net_tx_dirty/g_net_tx_dropped

// ( r4) [HB] attribution: set while parked in a network socket send (1=ws).
// Defined in Protocol.cpp; declared here to avoid pulling in all of Protocol.h.
extern volatile uint8_t g_out_netwrite;

namespace WebUI {
    class WSChannels;

    WSChannel::WSChannel(WebSocketsServer* server, uint8_t clientNum) : Channel("websocket"), _server(server), _clientNum(clientNum) {}

    int WSChannel::read() {
        if (_dead) {
            return -1;
        }
        // ( Stage-0/B4) atomic read-and-clear. Once pushRT runs on the wifi_task
        // (WS server) and read() on the poller, a plain int would tear/lose a realtime
        // byte (WebUI feedhold/reset). exchange returns -1 when none is pending.
        return _rtchar.exchange(-1, std::memory_order_acq_rel);
    }

    WSChannel::operator bool() const { return true; }

    size_t WSChannel::write(uint8_t c) { return write(&c, 1); }

    size_t WSChannel::write(const uint8_t* buffer, size_t size) {
        //  While STA is down the socket is half-open (no FIN/RST). Gate
        // before enqueueing so we don't fill the ring with undeliverable lines
        // (mirrors today's drop) — no free, no container access.
        if (buffer == NULL || _dead || WiFiConfig::staTxSuspect() || !size) {
            return 0;
        }

        bool complete_line = buffer[size - 1] == '\n';

        const uint8_t* out;
        size_t         outlen;
        if (_output_line.length() == 0 && complete_line) {
            // Avoid the overhead of std::string if the
            // input is a complete line and nothing is pending.
            out    = buffer;
            outlen = size;
        } else {
            // Otherwise collect input until we have a line.
            // ( review A1) This runs on output_loop (core 0). At the  heap
            // floor _output_line.append() can throw std::bad_alloc, which is uncaught
            // up through deliver_message -> output_loop -> std::terminate -> reboot
            // (the no-reboot rule). Contain it: drop the partial line + reap the
            // channel (mirrors the F1 fix in push()). WiFi is the lowest tier.
            try {
                _output_line.append((char*)buffer, size);
            } catch (...) {
                _output_line.clear();
                _dead.store(true, std::memory_order_release);
                return size;
            }
            if (!complete_line) {
                return size;  // still accumulating; nothing to enqueue yet
            }

            out    = (uint8_t*)_output_line.c_str();
            outlen = _output_line.length();
        }
        // ( Phase-2 Stage 1) Append the complete line to the TX ring instead
        // of doing the blocking sendBIN here. The drain (output_loop now,
        // wifi_task after Stage 2) performs the actual socket send. One record ==
        // one sendBIN frame, so framing is preserved exactly. Drop-on-full is the
        // lowest-tier safety valve (WiFi protects plotting/OLED/USB by dropping).
        if (_tx.push(out, (uint32_t)outlen)) {
            g_net_tx_dirty.store(true, std::memory_order_release);
        } else {
            g_net_tx_dropped.fetch_add(1, std::memory_order_relaxed);
        }
        // Clear the accumulation buffer on BOTH the enqueued and dropped paths.
        // This reset is the tail of the old write(); dropping it would grow
        // _output_line without bound — a -class heap leak.
        if (_output_line.length()) {
            _output_line = "";
        }

        return size;
    }

    // ( Phase-2 Stage 1) Drain the TX ring -> sendBIN. Runs on the single
    // output consumer task (output_loop now; wifi_task after Stage 2). Peek/commit:
    // the record is advanced past ONLY after a successful send, so a back-pressured
    // socket leaves it queued in FIFO order rather than losing it. Touching _server
    // here is exactly today's broadcast behavior (output_loop, not the poller).
    bool WSChannel::drainTx(uint8_t* buf, size_t cap, bool& committedAny) {
        int      drained = 0;
        uint32_t n;
        while (drained < TX_DRAIN_MAX_RECORDS) {
            if (!_tx.peek(buf, (uint32_t)cap, n)) {
                return false;  // ring empty
            }
            if (_dead || WiFiConfig::staTxSuspect()) {
                return true;  // not deliverable now; leave queued (not committed)
            }
            int stat = _server->canSend(_clientNum);
            if (stat < 0) {
                _dead.store(true, std::memory_order_release);
                log_debug("WebSocket is dead; closing");
                return false;
            }
            if (stat == 0) {
                return true;  // transient back-pressure: leave queued, retry next pass
            }
            g_out_netwrite = 1;  //  [HB] attribution: parked in a WS socket send
            bool ok        = _server->sendBIN(_clientNum, buf, n);
            g_out_netwrite = 0;
            if (!ok) {
#ifdef DEBUG_HEAP_INSTRUMENTATION
                // canSend said writable but the send still failed -> genuinely dead.
                log_info("WS-DROP sendBIN cn=" << (int)_clientNum
                         << " canSend=" << _server->canSend(_clientNum)
                         << " freeInternal=" << heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
                         << " largestInternal=" << heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#endif
                _dead.store(true, std::memory_order_release);
                log_debug("WebSocket is unresponsive; closing");
                return false;
            }
            _tx.commit(n);
            committedAny = true;
            ++drained;
        }
        return true;  // bound hit; more may remain
    }

    void WSChannel::pushRT(char ch) { _rtchar.store((uint8_t)ch, std::memory_order_release); }

    bool WSChannel::push(const uint8_t* data, size_t length) {
        if (_dead) {
            return false;
        }
        // ( Stage 2 / M4) Runs on the wifi_task (handleEvent). Lock _queue vs the
        // poller's pollLine front/pop/size (deque-realloc race). Cap by BOTH the frame
        // `length` and WS_PUSH_MAX: a BIN frame is not guaranteed NUL-terminated, so the
        // old NUL-scan could read past the payload; and the cap keeps the core-0 portMUX
        // critical section bounded. For TEXT (length excludes the NUL) this pushes the
        // same bytes as the old `!='\0'` loop.
        //
        // ( Stage 2, review F1) _queue.push() allocates a deque chunk and can throw
        // std::bad_alloc at the  OOM floor. lockInput()=portENTER_CRITICAL is NOT
        // RAII — an escaping throw would leak the spinlock with core-0 interrupts
        // disabled (HARD HANG) and is uncaught upstream (handleEvent only catches
        // out_of_range) -> abort/reboot. Both are the no-wedge/no-reboot spine. Contain
        // it: always portEXIT_CRITICAL, mark the channel dead, drop the frame.
        static const size_t WS_PUSH_MAX = 512;
        char                c;
        size_t              n = 0;
        lockInput();
        try {
            while (n < length && (c = *data++) != '\0' && n < WS_PUSH_MAX) {
                _queue.push(c);
                ++n;
            }
        } catch (...) {
            unlockInput();
            _dead.store(true, std::memory_order_release);
            return false;
        }
        unlockInput();
        return true;
    }

    bool WSChannel::push(std::string& s) { return push((uint8_t*)s.c_str(), s.length()); }

    bool WSChannel::sendTXT(std::string& s) {
        //  sendTXT runs on the WIFI_TASK via WSChannels::sendPing (webServer.handle,
        // moved off the poller in Stage 2). canSend() below returns >0 on a half-open
        // socket (no FIN/RST), so without this gate it enters the ~5s blocking
        // arduinoWebSockets write. That now only parks the wifi_task (harmless to the
        // real-time tiers), but the gate is still worth keeping: it avoids a pointless
        // ~5s wifi_task stall + re-filling the link while STA is down (heap protection).
        if (_dead || WiFiConfig::staTxSuspect()) {
            return false;
        }
        // (/ SF-6 + SF-1) Gate on canSend like write() already does
        // (WSChannel.cpp:59). HW-confirmed root cause of : at the low-heap
        // floor under upload+plot load, lwIP TX-pbuf exhaustion makes the socket
        // transiently NOT writable (canSend==0, largest-free-block ~2.3K). The
        // old code skipped the guard and let arduinoWebSockets' 5 s blocking
        // write fail, then KILLED the channel (internal tracker). Now: a not-writable
        // socket SKIPS this droppable frame (status/PING) and is NOT killed, and
        // we never enter the 5 s blocking write on the core-0 poller. Only a
        // genuinely disconnected socket (canSend<0) reaps the channel. The host
        // re-polls and the heartbeat re-emits on the next state edge once heap
        // recovers — so a heap dip no longer drops the link.
        int stat = _server->canSend(_clientNum);
        if (stat < 0) {
            _dead = true;
            log_debug("WebSocket is dead; closing");
            WSChannels::removeChannel(this);
            return false;
        }
        if (stat == 0) {
            return false;  // transient back-pressure — skip, do NOT kill
        }
        if (!_server->sendTXT(_clientNum, s.c_str())) {
#ifdef DEBUG_HEAP_INSTRUMENTATION
            // canSend said writable but the write still failed -> genuinely dead.
            // (/ attribution, debug-gated) capture heap state at this
            // (now-rare) drop.
            log_info("WS-DROP sendTXT cn=" << (int)_clientNum
                     << " canSend=" << _server->canSend(_clientNum)
                     << " freeInternal=" << heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
                     << " largestInternal=" << heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#endif
            _dead = true;
            log_debug("WebSocket is unresponsive; closing");
            WSChannels::removeChannel(this);
            return false;
        }
        return true;
    }

    void WSChannel::autoReport() {
        // ( Stage-0/B2) autoReport runs on the POLLER. Do NOT touch _server here:
        // once the WS server loop (which can disconnect/free _clients[num]->tcp) moves
        // to the wifi_task, _server->canSend() from the poller is a cross-task UAF.
        // Gate only on local atomic flags; the write-side decides deliverability and
        // drops if the socket can't take it.
        if (_dead || WiFiConfig::staTxSuspect()) {
            return;
        }
        Channel::autoReport();
    }

    WSChannel::~WSChannel() {}

    std::map<uint8_t, WSChannel*> WSChannels::_wsChannels;
    std::list<WSChannel*>         WSChannels::_webWsChannels;

    WSChannel* WSChannels::_lastWSChannel = nullptr;

    WSChannel* WSChannels::getWSChannel(int pageid) {
        WSChannel* wsChannel = nullptr;
        if (pageid != -1) {
            try {
                wsChannel = _wsChannels.at(pageid);
            } catch (std::out_of_range& oor) {}
        } else {
            // If there is no PAGEID URL argument, it is an old version of WebUI
            // that does not supply PAGEID in all cases.  In that case, we use
            // the most recently used websocket if it is still in the list.
            for (auto it = _wsChannels.begin(); it != _wsChannels.end(); ++it) {
                if (it->second == _lastWSChannel) {
                    wsChannel = _lastWSChannel;
                    break;
                }
            }
        }
        _lastWSChannel = wsChannel;
        return wsChannel;
    }

    void WSChannels::removeChannel(uint8_t num) {
        try {
            WSChannel* wsChannel = _wsChannels.at(num);
            wsChannel->markDead();  // ( Stage 1) an in-flight drain skips the socket op
            _webWsChannels.remove(wsChannel);
            allChannels.kill(wsChannel);
            _wsChannels.erase(num);
        } catch (std::out_of_range& oor) {}
    }

    void WSChannels::removeChannel(WSChannel* channel) {
        _lastWSChannel = nullptr;
        channel->markDead();  // ( Stage 1) an in-flight drain skips the socket op
        _webWsChannels.remove(channel);
        allChannels.kill(channel);
        for (auto it = _wsChannels.cbegin(); it != _wsChannels.cend();) {
            if (it->second == channel) {
                it = _wsChannels.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Tears down every tracked WSChannel by routing it through the normal
    // removeChannel path.  Called from Web_Server::end() before the backing
    // WebSocketsServer is deleted, so no WSChannel is left registered with
    // allChannels holding a dangling _server pointer.
    void WSChannels::removeAllChannels() {
        std::list<WSChannel*> toRemove;
        for (auto& pair : _wsChannels) {
            toRemove.push_back(pair.second);
        }
        for (WSChannel* channel : toRemove) {
            removeChannel(channel);
        }
    }

    bool WSChannels::runGCode(int pageid, std::string cmd) {
        bool has_error = false;

        WSChannel* wsChannel = getWSChannel(pageid);
        if (wsChannel) {
            // It is very tempting to let wsChannel->push() handle the realtime
            // character sequences so we don't have to do it here.  That does not work
            // because we need to know whether to add a newline.  We should not add newline
            // on a realtime sequence, but we must add one (if not already present)
            // on a text command.
            if (cmd.length() == 3 && cmd[0] == 0xc2 && is_realtime_command(cmd[1]) && cmd[2] == '\0') {
                // Handles old WebUIs that send a null after high-bit-set realtime chars
                wsChannel->pushRT(cmd[1]);
            } else if (cmd.length() == 2 && cmd[0] == 0xc2 && is_realtime_command(cmd[1])) {
                // Handles old WebUIs that send a null after high-bit-set realtime chars
                wsChannel->pushRT(cmd[1]);
            } else if (cmd.length() == 1 && is_realtime_command(cmd[0])) {
                wsChannel->pushRT(cmd[0]);
            } else {
                if (cmd.length() && cmd[cmd.length() - 1] != '\n') {
                    cmd += '\n';
                }
                has_error = !wsChannel->push(cmd);
            }
        } else {
            has_error = true;
        }
        return has_error;
    }

    bool WSChannels::sendError(int pageid, std::string err) {
        WSChannel* wsChannel = getWSChannel(pageid);
        if (wsChannel) {
            return !wsChannel->sendTXT(err);
        }
        return true;
    }
    void WSChannels::sendPing() {
        // Snapshot the list before iterating.  sendTXT() calls removeChannel()
        // on send failure, which in turn calls _webWsChannels.remove() -- if
        // that ran on the list we were iterating, it would invalidate the
        // current iterator and the next loop step would be undefined.
        std::list<WSChannel*> snapshot = _webWsChannels;
        for (WSChannel* wsChannel : snapshot) {
            std::string s("PING:");
            s += std::to_string(wsChannel->id());
            // sendBIN would be okay too because the string contains only
            // ASCII characters, no UTF-8 extended characters.
            wsChannel->sendTXT(s);
        }
    }

    void WSChannels::handleEvent(WebSocketsServer* server, uint8_t num, uint8_t type, uint8_t* payload, size_t length) {
        switch (type) {
            case WStype_DISCONNECTED:
                log_debug("WebSocket disconnect " << num);
                WSChannels::removeChannel(num);
                break;
            case WStype_CONNECTED: {
                // ( Stage 1, M-5) nothrow: the channel embeds a 512 B TX
                // ring, so `new` needs a larger contiguous block. At the  heap
                // floor that can fail; degrade to "no channel" (log + skip) rather
                // than let bad_alloc escape this callback and abort/reboot.
                WSChannel* wsChannel = new (std::nothrow) WSChannel(server, num);
                if (!wsChannel) {
                    log_error("Creating WebSocket channel failed");
                } else {
                    std::string uri((char*)payload, length);

                    IPAddress ip = server->remoteIP(num);
                    log_debug("WebSocket " << num << " from " << ip << " uri " << uri);

                    _lastWSChannel = wsChannel;
                    allChannels.registration(wsChannel);
                    _wsChannels[num] = wsChannel;

                    if (uri == "/") {
                        std::string s("CURRENT_ID:");
                        s += std::to_string(num);
                        // send message to client
                        _webWsChannels.push_front(wsChannel);
                        wsChannel->sendTXT(s);
                        s = "ACTIVE_ID:";
                        s += std::to_string(wsChannel->id());
                        wsChannel->sendTXT(s);
                    }
                    //  WiFi reconnect recovery: a fresh WS channel means a
                    // (re)connecting host. If we are paused with a cached
                    // instruction, re-emit it (to allChannels, now including this
                    // just-registered channel; idempotent on Studio). Self-gated,
                    // so a connect while not paused is a no-op. USB has no analog
                    // and instead answers the `?`/`$I` probe (Serial.cpp /
                    // ProcessSettings.cpp).
                    reemit_pause_instruction();
                }
            } break;
            case WStype_TEXT:
            case WStype_BIN:
                try {
                    _wsChannels.at(num)->push(payload, length);
                } catch (std::out_of_range& oor) {}
                break;
            default:
                break;
        }
    }
}
#endif
