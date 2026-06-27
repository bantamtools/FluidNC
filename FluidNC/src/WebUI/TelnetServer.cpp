// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.
#include <ESPmDNS.h>
#include <new>  // ( Stage 1) std::nothrow for client/channel allocation
#include "../Machine/MachineConfig.h"
#include "TelnetClient.h"
#include "TelnetServer.h"
#include "WebSettings.h"

#ifdef ENABLE_WIFI

namespace WebUI {
    TelnetServer telnetServer  __attribute__((init_priority(107))) ;
}

#    include "WifiServices.h"

#    include "WifiConfig.h"
#    include "../Report.h"  // report_init_message()
#    include "Commands.h"   // COMMANDS

#    include <WiFi.h>

namespace WebUI {

    EnumSetting* telnet_enable;
    IntSetting*  telnet_port;

    TelnetServer::TelnetServer() {
        telnet_port = new IntSetting(
            "Telnet Port", WEBSET, WA, "ESP131", "Telnet/Port", DEFAULT_TELNETSERVER_PORT, MIN_TELNET_PORT, MAX_TELNET_PORT, NULL);

        telnet_enable = new EnumSetting("Telnet Enable", WEBSET, WA, "ESP130", "Telnet/Enable", DEFAULT_TELNET_STATE, &onoffOptions, NULL);
    }

    bool TelnetServer::begin() {
        bool no_error = true;
        end();

        if (!WebUI::telnet_enable->get()) {
            return false;
        }
        _port = WebUI::telnet_port->get();

        //create instance
        _wifiServer = new WiFiServer(_port, MAX_TLNT_CLIENTS);
        _wifiServer->setNoDelay(true);
        log_info("Telnet started on port " << _port);
        //start telnet server
        _wifiServer->begin();
        _setupdone = true;

        //add mDNS
        MDNS.addService("telnet", "tcp", _port);

        return no_error;
    }

    void TelnetServer::end() {
        _setupdone = false;
        if (_wifiServer) {
            delete _wifiServer;
            _wifiServer = NULL;
        }

        //remove mDNS
        mdns_service_remove("_telnet", "_tcp");
    }

    void TelnetServer::handle() {
        if (!_setupdone || _wifiServer == NULL) {
            return;
        }

        while (true) {
            // ( Stage-0/M2) pop under the lock, then release BEFORE kill() (which
            // takes AllChannels::_mutex) — keeps the order _mutex -> leaf and never
            // holds the leaf lock across heavier work.
            TelnetClient* client = nullptr;
            {
                std::lock_guard<std::mutex> lock(_disconnectedMutex);
                if (_disconnected.empty()) {
                    break;
                }
                client = _disconnected.front();
                _disconnected.pop();
            }
            log_debug("Telnet client disconnected");
            // ( /  SF-2) Route teardown through the kill mechanism
            // instead of deregistration + a raw delete. The raw delete bypassed
            // the deferred-free guards (_broadcastDepth / pendingOut), so a
            // telnet disconnect coinciding with an in-flight broadcast snapshot
            // or a queued output message still referencing this client freed
            // memory still in use -- a UAF / heap corruption that can crash
            // (reboot) under connect/disconnect churn. kill() deregisters and
            // frees it only once no holder still references the pointer.
            allChannels.kill(client);
        }

        //check if there are any new clients
        if (_wifiServer->hasClient()) {
            WiFiClient* tcpClient = new (std::nothrow) WiFiClient(_wifiServer->available());
            if (!tcpClient) {
                log_error("Creating telnet client failed");
                return;
            }
            log_debug("Telnet from " << tcpClient->remoteIP());
            // ( Stage 1, M-5) nothrow + null-check: TelnetClient now embeds a
            // 512 B TX ring, so `new` needs a larger contiguous block that can fail
            // at the  heap floor. Degrade gracefully (free the socket, skip)
            // rather than let bad_alloc escape and abort/reboot. The pre-existing
            // code also dereferenced a null tcpClient and never null-checked tnc.
            TelnetClient* tnc = new (std::nothrow) TelnetClient(tcpClient);
            if (!tnc) {
                log_error("Creating telnet channel failed");
                delete tcpClient;
                return;
            }
            allChannels.registration(tnc);
        }
    }
    TelnetServer::~TelnetServer() { end(); }
}

#endif
