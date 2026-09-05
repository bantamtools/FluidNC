// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license (see LICENSE).
#pragma once
//
// The feed-hold / park / resume state machine, moved verbatim out of
// Protocol.cpp so it can be linked into the host pause simulator
// (env:pausesim). Function names and bodies are unchanged; only `static`
// dropped. See internal design notes
#include <cstdint>
#include "Report.h"  // Message

// Seam between the hold machine and the rest of the firmware. The firmware
// installs real functions at static-init; env:pausesim installs recorders.
struct HoldPorts {
    void (*report_realtime_status)();  // report_realtime_status(allChannels)
    void (*report_feedback_message)(Message);
    void (*oled_refresh)();             // config->_oled->refresh_display()
    void (*oled_clear_m0_comment)();    // config->_oled->clear_m0_comment()
    void (*emit_pause_instruction_clear)();
    void (*initiate_homing_cycle)();  // protocol_initiate_homing_cycle()
    void (*manage_spindle)();         // protocol_manage_spindle()
    void (*wifi_resume)();            // WiFi.setAutoReconnect(true)+reconnect; no-op without ENABLE_WIFI
};
const HoldPorts& hold_ports();
#ifdef PAUSESIM
void set_hold_ports(const HoldPorts& p);
#endif

// Spindle stop override control states (were file-scope statics in Protocol.cpp).
struct SpindleStopBits {
    uint8_t enabled : 1;
    uint8_t initiate : 1;
    uint8_t restore : 1;
    uint8_t restoreCycle : 1;
};
union SpindleStop {
    uint8_t         value;
    SpindleStopBits bit;
};
extern SpindleStop spindle_stop_ovr;

void protocol_start_holding();
void protocol_cancel_jogging();
void protocol_hold_complete();
void protocol_do_feedhold(void* arg);
void protocol_do_initiate_cycle();
void protocol_do_cycle_start();
void protocol_exec_rt_suspend();

// The Hold/SafetyDoor/Sleep branch of protocol_do_cycle_stop (was inline in that
// function in Protocol.cpp). The dispatcher stays in Protocol.cpp and calls this.
bool hold_cycle_stop();  // true = the Hold/SafetyDoor/Sleep case handled the stop; false = fall through
