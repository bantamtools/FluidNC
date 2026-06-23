// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "WSChannel.h"
#include <esp_heap_caps.h>  //  heap attribution at the WS-drop instant

#ifdef ENABLE_WIFI
#    include "WebServer.h"
#    include <WebSocketsServer.h>
#    include <WiFi.h>

#    include "../Serial.h"  // is_realtime_command

namespace WebUI {
    class WSChannels;

    WSChannel::WSChannel(WebSocketsServer* server, uint8_t clientNum) : Channel("websocket"), _server(server), _clientNum(clientNum) {}

    int WSChannel::read() {
        if (_dead) {
            return -1;
        }
        if (_rtchar == -1) {
            return -1;
        } else {
            auto ret = _rtchar;
            _rtchar  = -1;
            return ret;
        }
    }

    WSChannel::operator bool() const { return true; }

    size_t WSChannel::write(uint8_t c) { return write(&c, 1); }

    size_t WSChannel::write(const uint8_t* buffer, size_t size) {
        if (buffer == NULL || _dead || !size) {
            return 0;
        }

        bool complete_line = buffer[size - 1] == '\n';

        const uint8_t* out;
        size_t         outlen;
        if (_output_line.length() == 0 && complete_line) {
            // Avoid the overhead of std::string if we the
            // input is a complete line and nothing is pending.
            out    = buffer;
            outlen = size;
        } else {
            // Otherwise collect input until we have line.
            _output_line.append((char*)buffer, size);
            if (!complete_line) {
                return size;
            }

            out    = (uint8_t*)_output_line.c_str();
            outlen = _output_line.length();
        }
        int stat = _server->canSend(_clientNum);
        if (stat < 0) {
            _dead = true;
            log_debug("WebSocket is dead; closing");
            return 0;
        }
        if (stat == 0) {
            if (_output_line.length()) {
                _output_line = "";
            }
            return size;
        }
        if (!_server->sendBIN(_clientNum, out, outlen)) {
#ifdef DEBUG_HEAP_INSTRUMENTATION
            // (/ attribution, debug-gated) capture heap state at the drop
            // instant — distinguishes WS-malloc pressure (tiny largest-block) from
            // lwIP TX-pbuf exhaustion (canSend>0 + ample largest-block, send fails).
            log_info("WS-DROP sendBIN cn=" << (int)_clientNum
                     << " canSend=" << _server->canSend(_clientNum)
                     << " freeInternal=" << heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
                     << " largestInternal=" << heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#endif
            _dead = true;
            log_debug("WebSocket is unresponsive; closing");
        }
        if (_output_line.length()) {
            _output_line = "";
        }

        return size;
    }

    void WSChannel::pushRT(char ch) { _rtchar = ch; }

    bool WSChannel::push(const uint8_t* data, size_t length) {
        if (_dead) {
            return false;
        }
        char c;
        while ((c = *data++) != '\0') {
            _queue.push(c);
        }
        return true;
    }

    bool WSChannel::push(std::string& s) { return push((uint8_t*)s.c_str(), s.length()); }

    bool WSChannel::sendTXT(std::string& s) {
        if (_dead) {
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
        int stat = _server->canSend(_clientNum);
        if (stat > 0) {
            Channel::autoReport();
        }
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
            _webWsChannels.remove(wsChannel);
            allChannels.kill(wsChannel);
            _wsChannels.erase(num);
        } catch (std::out_of_range& oor) {}
    }

    void WSChannels::removeChannel(WSChannel* channel) {
        _lastWSChannel = nullptr;
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
                WSChannel* wsChannel = new WSChannel(server, num);
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
