// Copyright (c) 2011-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Types.h"

#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Config.h"
#include "WebUI/Authentication.h"
#include "InputFile.h"
#include "Flashing.h"
#ifdef USE_SDMMC
#include "Driver/sdmmc.h"
#else
#include "Driver/sdspi.h"
#endif

// Line buffer size from the serial input stream to be executed.Also, governs the size of
// each of the startup blocks, as they are each stored as a string of this size.
//
// NOTE: Not a problem except for extreme cases, but the line buffer size can be too small
// and g-code blocks can get truncated. Officially, the g-code standards support up to 256
// characters. In future versions, this will be increased, when we know how much extra
// memory space we can invest into here or we re-write the g-code parser not to have this
// buffer.

const int LINE_BUFFER_SIZE = 256;

void protocol_reset();

void protocol_init();

void protocol_read_encoder();
void protocol_read_ultrasonic();

// Starts the main loop. It handles all incoming characters from the serial port and executes
// them as they complete. It is also responsible for finishing the initialization procedures.
void protocol_main_loop();

// Checks and executes a realtime command at various stop points in main program
void protocol_execute_realtime();
void protocol_exec_rt_system();

// Executes the auto cycle feature, if enabled.
void protocol_auto_cycle_start();

// Block until all buffered steps are executed
void protocol_buffer_synchronize();

// Disables the stepper motors or schedules it to happen
void protocol_disable_steppers();
void protocol_cancel_disable_steppers();

extern volatile bool rtReset;
extern volatile bool rtCycleStop;

extern volatile bool runLimitLoop;

// RC servo calibration variables
extern volatile bool rcServoZCal;
extern float rcServoZOriginalPos;

//  Full-mute diagnostics (defined in Protocol.cpp). Query-only liveness +
// TX-stall attribution surfaced via [ESP420]; the USB ones are written by the USB
// TX path (Usb.cpp). Frozen out_hb/poll_hb or a large usb age at the moment of
// silence discriminates a software stall from a USB-Serial-JTAG peripheral drop.
extern volatile uint32_t g_output_hb;
extern volatile uint32_t g_poll_hb;
extern volatile uint32_t g_wifi_hb;  // ( Stage 2) wifi_task liveness (starvation check)
extern volatile uint32_t g_last_deliver_ms;
extern volatile uint32_t g_max_write_ms;
extern volatile uint32_t g_write_stalls;
extern char              g_stuck_channel[24];
extern volatile uint32_t g_usb_tx_drops;
extern volatile uint32_t g_usb_last_tx_ms;
extern volatile uint8_t  g_poll_phase;        //  which polling_loop call is active
extern char              g_poll_chan[24];     // ( r3) channel whose pollLine the poller is in
extern volatile uint32_t g_bcast_slow_ms;     //  worst per-leaf broadcast write
extern char              g_bcast_slow_leaf[24];
extern volatile uint32_t g_bcast_slow_count;
extern volatile uint8_t  g_wifi_svc;          // ( r4) WiFi service the wifi_task is in: 1=OTA 2=web 3=telnet 4=rss 0=none
extern volatile uint8_t  g_out_netwrite;      // ( r4) wifi_task parked in a network leaf send (drainTx): 1=ws 2=telnet 0=none

// RC servo calibration functions
void clearRcServoCalibration();

// Alarm codes.
enum class ExecAlarm : uint8_t {
    None                  = 0,
    HardLimit             = 1,
    SoftLimit             = 2,
    AbortCycle            = 3,
    ProbeFailInitial      = 4,
    ProbeFailContact      = 5,
    HomingFailReset       = 6,
    HomingFailDoor        = 7,
    HomingFailPulloff     = 8,
    HomingFailApproach    = 9,
    SpindleControl        = 10,
    ControlPin            = 11,
    HomingAmbiguousSwitch = 12,
};

//  Deterministic open of an SD file that was queued to run after an
// auto-home. Set by the file-run handlers; consumed by Homing::done; the
// failure/clear hooks discard a pending path so a later $H can't run it.
void protocol_set_pending_file(const std::string& path);
void protocol_clear_pending_file();
void protocol_run_pending_file_after_homing();   // call from Homing::done after _homed
void protocol_on_homing_failed();                 // call from Homing::fail

// Shared auto-home-before-run used by every file launcher (OLED file menu,
// Run Again/Next/Latest, Studio/serial $SD/Run). Returns true if it started a
// homing cycle and stashed sdPath to auto-run on completion (caller must NOT
// open the file itself); false if the caller should open immediately.
bool protocol_home_before_run_if_needed(const char* sdPath);

extern volatile ExecAlarm rtAlarm;  // Global realtime executor variable for setting various alarms.

#include <map>
extern std::map<ExecAlarm, const char*> AlarmNames;

#include "Event.h"
enum AccessoryOverride {
    SpindleStopOvr = 1,
    FloodToggle    = 2,
    MistToggle     = 3,
};

extern ArgEvent feedOverrideEvent;
extern ArgEvent rapidOverrideEvent;
extern ArgEvent spindleOverrideEvent;
extern ArgEvent accessoryOverrideEvent;
extern ArgEvent limitEvent;
extern ArgEvent cardDetectEvent;

extern ArgEvent reportStatusEvent;

extern NoArgEvent safetyDoorEvent;
extern ArgEvent feedHoldEvent;
extern NoArgEvent cycleStartEvent;
extern NoArgEvent cycleStopEvent;
extern NoArgEvent motionCancelEvent;
extern NoArgEvent sleepEvent;
extern NoArgEvent resetEvent;
extern NoArgEvent debugEvent;
extern NoArgEvent enterEvent;

// extern NoArgEvent statusReportEvent;

extern xQueueHandle event_queue;

extern bool pollingPaused;

// ( Stage 2) The core-0 poller task handle. Externed so settings handlers that
// rebuild the OLED menu (which the poller renders) can vTaskSuspend it during the
// rebuild — the wifi-off setting now runs on the wifi_task (HTTP) or core-1 ($ESP),
// both racing the poller's menu render. Also: the wifi_task handle (lifecycle owner).
extern TaskHandle_t pollingTask;
extern TaskHandle_t wifiTask;

struct EventItem {
    Event* event;
    void*  arg;
};

void protocol_send_event(Event*, void* arg = 0);
void protocol_handle_events();

inline void protocol_send_event(Event* evt, int arg) {
    protocol_send_event(evt, (void*)arg);
}

void protocol_send_event_from_ISR(Event* evt, void* arg = 0);

void send_line(Channel& channel, const char* message);
void send_line(Channel& channel, const std::string* message);
void send_line(Channel& channel, const std::string& message);

//  Explicit-droppability overloads — force a bounded-wait (droppable=false
// => 250ms then drop) delivery for a NON-ack protocol token. The default
// overloads above hardcode droppable = !is_ack_line(message), so every [MSG:...]
// token is enqueued droppable (wait=0). Best-effort, NOT a delivery guarantee:
// enqueue collapses the wait to 0 on the output task, and the WiFi TxRing<512>
// still drops-on-full downstream. Used by the  pause-instruction emit.
void send_line(Channel& channel, const char* message, bool droppable);
void send_line(Channel& channel, const std::string* message, bool droppable);
void send_line(Channel& channel, const std::string& message, bool droppable);

void drain_messages();

extern uint32_t heapLowWater;
