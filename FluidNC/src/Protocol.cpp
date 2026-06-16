// Copyright (c) 2011-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
  Protocol.cpp - execution state machine
*/

#include "Protocol.h"
#include "CompletionMark.h"  // : strip-on-start helper
#include "Config.h"
#include "Error.h"
#include "Event.h"
#include "Menu.h"

#include "Flashing.h"
#include "Logging.h"
#include "Machine/MachineConfig.h"
#include "Machine/Homing.h"
#include "Report.h"         // report_feedback_message
#include "Limits.h"         // limits_get_state, soft_limit
#include "Planner.h"        // plan_get_current_block
#include "MotionControl.h"  // PARKING_MOTION_LINE_NUMBER
#include "Settings.h"       // settings_execute_startup
#include "SettingsDefinitions.h"  // : completion_marking
#include "Machine/LimitPin.h"
#include "System.h"
#include "WebUI/RSSReader.h"
#include "WebUI/WifiConfig.h"
#include "SSD1306_I2C.h"  // synchronous OLED flush before reboot
#include "SDFiles/SDFileTable.h"  // sd_table().mutex() — arena lock for the run-file path
#include "SDFiles/SDBrowser.h"    // SDBrowser::activate
#ifdef ENABLE_WIFI
#    include <WiFi.h>
#endif

#include <filesystem>
#include <string>
#include <cstring>  // strcmp
#include <mutex>    // std::unique_lock — arena lock across activate -> path copy-out

// External function for config recovery
extern void copyRecoveryConfigAndRestart();

volatile ExecAlarm rtAlarm;  // Global realtime executor bitflag variable for setting various alarms.

std::map<ExecAlarm, const char*> AlarmNames = {
    { ExecAlarm::None, "None" },
    { ExecAlarm::HardLimit, "Hard Limit" },
    { ExecAlarm::SoftLimit, "Soft Limit" },
    { ExecAlarm::AbortCycle, "Abort Cycle" },
    { ExecAlarm::ProbeFailInitial, "Probe Fail Initial" },
    { ExecAlarm::ProbeFailContact, "Probe Fail Contact" },
    { ExecAlarm::HomingFailReset, "Homing Fail Reset" },
    { ExecAlarm::HomingFailDoor, "Homing Fail Door" },
    { ExecAlarm::HomingFailPulloff, "Homing Fail Pulloff" },
    { ExecAlarm::HomingFailApproach, "Homing Fail Approach" },
    { ExecAlarm::SpindleControl, "Spindle Control" },
    { ExecAlarm::ControlPin, "Control Pin Initially On" },
    { ExecAlarm::HomingAmbiguousSwitch, "Ambiguous Switch" },
};

volatile bool rtReset;

static volatile bool rtSafetyDoor;

volatile bool runLimitLoop;  // Interface to show_limits()

static void protocol_exec_rt_suspend();

static char line[LINE_BUFFER_SIZE];     // Line to be executed. Zero-terminated.
static char comment[LINE_BUFFER_SIZE];  // Line to be executed. Zero-terminated.
// static uint8_t line_flags           = 0;
// static uint8_t char_counter         = 0;
// static uint8_t comment_char_counter = 0;

volatile bool rcServoZCal = false;
float rcServoZOriginalPos = -99999.0f;

void clearRcServoCalibration() {
    rcServoZCal = false;
    rcServoZOriginalPos = -99999.0f;
}

// Spindle stop override control states.
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

static SpindleStop spindle_stop_ovr;

// Forward declarations for the polling-task / main-loop handshake globals
// defined later in this file. protocol_reset() clears them so a line that
// was grabbed by the polling task but not yet dispatched does not survive
// the reset boundary.
extern Channel* activeChannel;
extern char     activeLine[];

void protocol_reset() {
    probeState             = ProbeState::Off;
    soft_limit             = false;
    rtReset                = false;
    rtSafetyDoor           = false;
    spindle_stop_ovr.value = 0;

    // Drop any line the polling task had stashed for the main loop but
    // had not yet been dispatched. After reset, gc_init() zeros gc_state
    // (including feed_rate); without this clear, the next iteration of
    // protocol_main_loop would dispatch the stale line against a freshly
    // zeroed parser and raise Error 22 (Gcode undefined feed rate) for any
    // G1/G2/G3 line that relies on the prior modal feed rate. The pointer
    // may also reference a channel that is in the kill queue and about to
    // be deleted; clearing here prevents a downstream dereference.
    activeChannel = nullptr;
    activeLine[0] = '\0';

    // Do not clear rtAlarm because it might have been set during configuration
    // rtAlarm = ExecAlarm::None;
}

static int32_t idleEndTime = 0;


/*
  PRIMARY LOOP:
*/
static void request_safety_door() {
    rtSafetyDoor = true;
}

TaskHandle_t outputTask = nullptr;

xQueueHandle message_queue;

struct LogMessage {
    Channel* channel;
    void*    line;
    bool     isString;
};

// Count of messages dropped because the output queue stayed full past the
// bounded enqueue wait. Nonzero means a channel was not draining (e.g. the host
// stopped reading); the data is discarded rather than wedging the producer.
static uint32_t messages_dropped = 0;

// Deliver one queued message to its channel, reclaiming a heap string payload.
static void deliver_message(const LogMessage& msg) {
    if (msg.channel) {
        if (msg.isString) {
            std::string* s = static_cast<std::string*>(msg.line);
            if (s) {
                msg.channel->println(s->c_str());
            }
            delete s;
        } else {
            const char* cp = static_cast<const char*>(msg.line);
            if (cp) {
                msg.channel->println(cp);
            }
        }
        // Release the reference taken in enqueue_message AFTER the last use of
        // msg.channel above. While this is nonzero the kill-drain defers
        // freeing the channel, so a delivery in progress keeps the channel
        // alive for its whole duration.
        msg.channel->pendingOutDec();
    } else if (msg.isString) {
        delete static_cast<std::string*>(msg.line);
    }
}

// "ok" and "error:N" are the gcode-protocol acknowledgements a sender blocks on
// before sending the next line; everything else (status reports, [MSG:...]
// logs) is informational and may be dropped under back-pressure.
static inline bool is_ack_line(const char* s) {
    return s && ((s[0] == 'o' && s[1] == 'k' && s[2] == '\0') || strncmp(s, "error:", 6) == 0);
}

// Enqueue a message for the output task with a BOUNDED wait and a drop policy.
// The historical code spun forever in xQueueSend when the 10-deep queue was
// full, so a single blocked/slow channel write back-pressured and wedged every
// task that logged or acked -- including the protocol task . Now no
// caller ever blocks indefinitely:
//   - The output task is the SOLE drainer; it must never wait on its own
//     queue, because the space it would wait for can only be freed by itself.
//     A message it generates while delivering another (e.g. a log emitted
//     from a channel's parse path) is enqueued with zero wait and delivered
//     on a later iteration of its top-level loop; it is dropped only when
//     the queue is already full.
//   - Acks get a short bounded wait, then drop; informational lines drop
//     immediately when the queue is full.
static void enqueue_message(LogMessage& msg, bool droppable) {
    // Count this queued reference to the channel up front; deliver_message
    // releases it after delivery, and every drop path below releases it too.
    // The kill-drain in AllChannels::pollLine() will not free the channel
    // while the count is nonzero.
    if (msg.channel) {
        msg.channel->pendingOutInc();
    }
    TickType_t wait = droppable ? 0 : pdMS_TO_TICKS(250);
    if (outputTask && xTaskGetCurrentTaskHandle() == outputTask) {
        wait = 0;  // never block the sole drainer, even for an ack
    }
    if (xQueueSend(message_queue, &msg, wait)) {
        return;
    }
    if (msg.channel) {
        msg.channel->pendingOutDec();
    }
    if (msg.isString) {
        delete static_cast<std::string*>(msg.line);
    }
    ++messages_dropped;
}

void drain_messages() {
    while (uxQueueMessagesWaiting(message_queue)) {
        vTaskDelay(1);  // Let the output task finish sending data
    }
}

// This overload is used primarily with fixed string
// values.  It sends a pointer to the string whose
// memory does not need to be reclaimed later.
// This is the most efficient form, but it only works
// with fixed messages.
void send_line(Channel& channel, const char* line) {
    if (outputTask) {
        LogMessage msg { &channel, (void*)line, false };
        enqueue_message(msg, !is_ack_line(line));
    } else {
        channel.println(line);
    }
}

// This overload is used primarily with log_*() where
// a std::string is dynamically allocated with "new",
// and then extended to construct the message.  Its
// pointer is sent to the output task, which sends
// the message to the output channel and then "delete"s
// the pointer to reclaim the memory.
// This form has intermediate efficiency, as the string
// is allocated once and freed once.
void send_line(Channel& channel, const std::string* line) {
    if (outputTask) {
        LogMessage msg { &channel, (void*)line, true };
        enqueue_message(msg, !is_ack_line(line->c_str()));
    } else {
        channel.println(line->c_str());
        delete line;
    }
}

// This overload is used for many miscellaneous messages
// where the std::string is allocated in a code block and
// then extended with various information.  This send_line()
// copies that string to a newly allocated one and sends that
// via the std::string* version of send_line().  The original
// string is freed by the caller sometime after send_line()
// returns, while the new string is freed by the output task
// after the message is forwared to the output channel.
// This is the least efficient form, requiring two strings
// to be allocated and freed, with an intermediate copy.
// It is used only rarely.
void send_line(Channel& channel, const std::string& line) {
    if (outputTask) {
        send_line(channel, new std::string(line));
    } else {
        channel.println(line.c_str());
    }
}

void output_loop(void* unused) {
#ifdef DEBUG_MEMORY_WATERMARKS
    uint32_t start_time = millis();
#endif
    while (true) {
        LogMessage message;
        if (xQueueReceive(message_queue, &message, 0)) {
            deliver_message(message);
        }
        vTaskDelay(0);
#ifdef DEBUG_MEMORY_WATERMARKS
        if (millis() - start_time >= DEBUG_MEMORY_WM_TIME_MS) {
            log_warn("output_loop watermark -> " << uxTaskGetStackHighWaterMark(NULL));
            start_time = millis();
        }
#endif
    }
}

Channel* activeChannel = nullptr;  // Channel associated with the input line

TaskHandle_t pollingTask = nullptr;

char activeLine[Channel::maxLine];

bool pollingPaused = false;
void polling_loop(void* unused) {
#ifdef DEBUG_MEMORY_WATERMARKS
    uint32_t start_time = millis();
#endif
    // Poll the input sources waiting for a complete line to arrive
    for (; true; /*feedLoopWDT(), */ vTaskDelay(0)) {

        // Polling is paused when xmodem is using a channel for binary upload
        if (pollingPaused) {
            // Keep display alive during xmodem transfers.
            // INVARIANT: Safe to call updateBusyScreen() here only because
            // pollingPaused is set exclusively when xmodemReceive() blocks
            // the main loop — no concurrent buffer access.
            if (config->_oled) {
                config->_oled->updateBusyScreen();
                config->_oled->processDisplayRefresh();
            }
            vTaskDelay(100);
            continue;
        }

        // Read ultrasonic sensor
        // protocol_read_ultrasonic(); // DISABLED 2025-08-29 --WHO.
        
        // Process display refresh if needed
        if (config->_oled) {
            config->_oled->processDisplayRefresh();
        }

        if (activeChannel) {
            // Poll for realtime characters when waiting for the primary loop
            // (in another thread) to pick up the line.
            pollChannels();
            continue;
        }

        // Polling without an argument both checks for realtime characters and
        // returns a line-oriented command if one is ready.
        activeChannel = pollChannels(activeLine);
#ifdef DEBUG_MEMORY_WATERMARKS
        if (millis() - start_time >= DEBUG_MEMORY_WM_TIME_MS) {
            log_warn("polling_loop watermark -> " << uxTaskGetStackHighWaterMark(NULL));
            start_time = millis();
        }
#endif
    }
}

// #define CORE0_WDT
#ifdef CORE0_WDT

// Shared variable to indicate task status
volatile uint32_t core0_task_heartbeat = 0; // Increment this every second from the main task.

void core1_watchdog_task(void* pvParameters) {
    uint32_t last_heartbeat = core0_task_heartbeat;
    const TickType_t xDelay = pdMS_TO_TICKS(5000);

    for (;;) {
        vTaskDelay(xDelay);

        if (core0_task_heartbeat == last_heartbeat) {
            // No heartbeat update, task on core 0 might be stuck
            // ESP_LOGE(TAG, "Core 0 task unresponsive! Taking corrective action.");
            // Take corrective action, e.g., reset the system
            // esp_restart();
            // config->_oled->refresh_display();
            log_warn("Timeout detected in core1_watchdog_task")
            // if(config->_control->enter_locked()){
            //     log_warn("    Enter Locked in Timeout");
            // } else {
            //     log_warn("    Enter Unlocked in Timeout");
            // }
        } else {
            // Heartbeat updated, task is alive
            last_heartbeat = core0_task_heartbeat;
            log_debug("Core1 Watchdog, from core: " << xPortGetCoreID());
            log_debug("\t" << config->_oled->_state);
        }
    }
}

void do_heartbeat() {
    static int64_t last_heartbeat = 0;
    int64_t heartbeat_difference;
    int64_t current_time = esp_timer_get_time();
    static int hb_10ms_timer = 0;
    static int hb_100ms_timer = 0;
    static int hb_1sec_timer = 0;
    static int64_t n_heartbeats = 0;
    

    if (last_heartbeat == 0) {
        last_heartbeat = current_time;
        return; // Skip processing on the first call
    }

    heartbeat_difference = current_time - last_heartbeat;
    
    // Process elapsed time in increments of 10ms (10,000 microseconds)
    while (heartbeat_difference >= 10000) { // 10ms intervals
        heartbeat_difference -= 10000;
        last_heartbeat += 10000;
        hb_10ms_timer++;

        // Every 100ms
        if (hb_10ms_timer >= 10) {
            hb_10ms_timer = 0;
            hb_100ms_timer++;

            // Every 1 second
            if (hb_100ms_timer >= 10) {
                hb_100ms_timer = 0;
                hb_1sec_timer++;
                core0_task_heartbeat++; // Increment for core1 WDT
                // config->_oled->refresh_display(); // Force screen refresh every second?

                // Every 10 seconds
                if (hb_1sec_timer >= 10) {
                    hb_1sec_timer = 0;
                    n_heartbeats++;

                    // Trigger heartbeat action here
                    log_debug("Heartbeat from core " << xPortGetCoreID() << ": " << std::to_string(n_heartbeats));
                }
            }
        }
    }
}
#endif

void stop_polling() {
    if (pollingTask) {
        vTaskSuspend(pollingTask);
    }
}

// Use this to spinup various support tasks.
// For example, polling, logging and WDT monitoring
void start_polling() {
    if (pollingTask) {
        vTaskResume(pollingTask);
    } else {
        // log_debug("Start Polling Task");
        xTaskCreatePinnedToCore(polling_loop,      // task
                                "poller",          // name for task
                                6144,              // size of task stack
                                0,                 // parameters
                                1,                 // priority
                                &pollingTask,      // task handle
                                SUPPORT_TASK_CORE  // core
        );
        xTaskCreatePinnedToCore(output_loop,  // task
                                "output",     // name for task
                                4096,
                                // 16000,              // size of task stack
                                0,                 // parameters
                                1,                 // priority
                                &outputTask,       // task handle
                                SUPPORT_TASK_CORE  // core
        );
#ifdef CORE0_WDT
        xTaskCreatePinnedToCore(core1_watchdog_task,    // Task
                                "WatchdogTask",         // Name
                                2048,                   // Stack Size
                                NULL,                   // Parameters
                                1,                      // Priority
                                nullptr,                // Task Handle (nullptr)
                                SUPPORT_TASK_CORE     // Which core to run on
        );
#endif
    }
}

static void check_startup_state() {
    // Check for and report alarm state after a reset, error, or an initial power up.
    // NOTE: Sleep mode disables the stepper drivers and position can't be guaranteed.
    // Re-initialize the sleep state as an ALARM mode to ensure user homes or acknowledges.
    if (sys.state == State::ConfigAlarm) {
        report_error_message(Message::ConfigAlarmLock);
        // Add popup for recovery
        if (config && config->_oled) {
            config->_oled->popup_msg(
                "Config file error.\nPress button to load recovery config and restart.",
                0
            );
        }
    } else {
        // Perform some machine checks to make sure everything is good to go.
        if (config->_start->_checkLimits && config->_axes->hasHardLimits()) {
            if (limits_get_state()) {
                sys.state = State::Alarm;  // Ensure alarm state is active.
                report_error_message(Message::CheckLimits);
            }
        }
        if (config->_control->startup_check()) {
            rtAlarm = ExecAlarm::ControlPin;
        } else if (sys.state == State::Alarm || sys.state == State::Sleep) {
            report_feedback_message(Message::AlarmLock);
            sys.state = State::Alarm;  // Ensure alarm state is set.
        } else {
            // All systems go!
            sys.state = State::Idle;
            settings_execute_startup();  // Execute startup script.
        }
    }
}

const uint32_t heapWarnThreshold = 15000;

uint32_t heapLowWater = UINT_MAX;

void protocol_main_loop() {
    log_debug("Running task from core:" << xPortGetCoreID());
    check_startup_state();
    start_polling();

#ifdef DEBUG_MEMORY_WATERMARKS
    uint32_t start_time = millis();
#endif

    // ---------------------------------------------------------------------------------
    // Primary loop! Upon a system abort, this exits back to main() to reset the system.
    // This is also where the system idles while waiting for something to do.
    // ---------------------------------------------------------------------------------

    for (;; vTaskDelay(0)) {
#ifdef CORE0_WDT
        do_heartbeat();
#endif
        if (activeChannel) {
            // The input polling task has collected a line of input
#ifdef DEBUG_REPORT_ECHO_RAW_LINE_RECEIVED
            report_echo_line_received(activeLine, allChannels);
#endif

            // Detect external gcode activity for busy screen
            // Ping only for non-query gcode from external channels
            if (config->_oled &&
                activeLine[0] != '$' && activeLine[0] != '?' &&
                activeLine[0] != '[' && activeLine[0] != '\0' &&
                strcmp(activeChannel->name(), "file") != 0) {
                config->_oled->busyPing();
            }

            Error status_code = execute_line(activeLine, *activeChannel, WebUI::AuthenticationLevel::LEVEL_GUEST);

            // Tell the channel that the line has been processed.
            activeChannel->ack(status_code);

            // Tell the input polling task that the line has been processed,
            // so it can give us another one when available
            activeChannel = nullptr;
        }

        // Auto-cycle start any queued moves.
        protocol_auto_cycle_start();
        protocol_execute_realtime();  // Runtime command check point.

        // BusyScreen: always call updateBusyScreen (handles Off→Stage1
        // transition), then cleanup when not busy.
        if (config->_oled) {
            config->_oled->updateBusyScreen();
            if (!config->_oled->isBusy()) {
                config->_oled->cleanupAfterBusy();
            }
        }

        if (sys.abort) {
            stop_polling();
            return;  // Bail to main() program loop to reset system.
        }

        // check to see if we should disable the stepper drivers
        // If idleEndTime is 0, no disable is pending.

        // "(ticks() - EndTime) > 0" is a twos-complement arithmetic trick
        // for avoiding problems when the number space wraps around from
        // negative to positive or vice-versa.  It always works if EndTime
        // is set to "timer() + N" where N is less than half the number
        // space.  Using "timer() > EndTime" fails across the positive to
        // negative transition using signed comparison, and across the
        // negative to positive transition using unsigned.

        if (idleEndTime && (getCpuTicks() - idleEndTime) > 0) {
            idleEndTime = 0;  //
            config->_axes->set_disable(true);
        }
        uint32_t newHeapSize = xPortGetFreeHeapSize();
        if (newHeapSize < heapLowWater) {
            heapLowWater = newHeapSize;
            if (heapLowWater < heapWarnThreshold) {
                log_warn("Low memory: " << heapLowWater << " bytes");
            }
        }

#ifdef DEBUG_MEMORY_WATERMARKS
        if (millis() - start_time >= DEBUG_MEMORY_WM_TIME_MS) {
            log_warn("loop_task watermark -> " << uxTaskGetStackHighWaterMark(NULL));
            start_time = millis();
        }
#endif
    }
    return; /* Never reached */
}

// Block until all buffered steps are executed or in a cycle state. Works with feed hold
// during a synchronize call, if it should happen. Also, waits for clean cycle end.
void protocol_buffer_synchronize() {
    do {
        // Restart motion if there are blocks in the planner queue
        protocol_auto_cycle_start();
        protocol_execute_realtime();  // Check and execute run-time commands
        if (sys.abort) {
            return;  // Check for system abort
        }
    } while (plan_get_current_block() || (sys.state == State::Cycle));
}

// Auto-cycle start triggers when there is a motion ready to execute and if the main program is not
// actively parsing commands.
// NOTE: This function is called from the main loop, buffer sync, and mc_move_motors() only and executes
// when one of these conditions exist respectively: There are no more blocks sent (i.e. streaming
// is finished, single commands), a command that needs to wait for the motions in the buffer to
// execute calls a buffer sync, or the planner buffer is full and ready to go.
void protocol_auto_cycle_start() {
    // log_info("Protocol auto cycle start");
    if (plan_get_current_block() != NULL && sys.state != State::Cycle &&
        sys.state != State::Hold) {             // Check if there are any blocks in the buffer.
        protocol_send_event(&cycleStartEvent);  // If so, execute them
    }
}

// This function is the general interface to the real-time command execution system. It is called
// from various check points in the main program, primarily where there may be a while loop waiting
// for a buffer to clear space or any point where the execution time from the last check point may
// be more than a fraction of a second. This is a way to execute realtime commands asynchronously
// (aka multitasking) with g-code parsing and planning functions. This function also serves
// as an interface for the interrupts to set the system realtime flags, where only the main program
// handles them, removing the need to define more computationally-expensive volatile variables. This
// also provides a controlled way to execute certain tasks without having two or more instances of
// the same task, such as the planner recalculating the buffer upon a feedhold or overrides.
// NOTE: The sys_rt_exec_state.bit variable flags are set by any process, step or serial interrupts, pinouts,
// limit switches, or the main program.
void protocol_execute_realtime() {
    protocol_exec_rt_system();
    if (sys.suspend.value) {
        protocol_exec_rt_suspend();
    }
}

static void alarm_msg(ExecAlarm alarm_code) {
    log_to(allChannels, "ALARM:", static_cast<int>(alarm_code));
    delay_ms(500);  // Force delay to ensure message clears serial write buffer.
}

// Executes run-time commands, when required. This function is the primary state
// machine that controls the various real-time features.
// NOTE: Do not alter this unless you know exactly what you are doing!
static void protocol_do_alarm() {
    if (rtAlarm == ExecAlarm::None) {
        return;
    }

    // Clear RC servo calibration state on any alarm
    clearRcServoCalibration();
    log_info("RC servo calibration cleared due to alarm");

    if (spindle->_off_on_alarm) {
        spindle->stop();
    }
    sys.state = State::Alarm;  // Set system alarm state
#ifdef ENABLE_WIFI
    WiFi.setAutoReconnect(true);
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
    }
#endif
    alarm_msg(rtAlarm);
    if (rtAlarm == ExecAlarm::HardLimit || rtAlarm == ExecAlarm::SoftLimit) {
        if(rtAlarm == ExecAlarm::SoftLimit){
          report_error_message(Message::SoftLimitLock);
        } else {
          report_error_message(Message::CriticalEvent);
        }
        
        protocol_disable_steppers();
        rtReset = false;  // Disable any existing reset
        do {
            protocol_handle_events();
            // Block everything except reset and status reports until user issues reset or power
            // cycles. Hard limits typically occur while unattended or not paying attention. Gives
            // the user and a GUI time to do what is needed before resetting, like killing the
            // incoming stream. The same could be said about soft limits. While the position is not
            // lost, continued streaming could cause a serious crash if by chance it gets executed.
        } while (!rtReset);
    }
    rtAlarm = ExecAlarm::None;
}

static void protocol_start_holding() {
    if (!(sys.suspend.bit.motionCancel || sys.suspend.bit.jogCancel)) {  // Block, if already holding.
        sys.step_control = {};
        if (!Stepper::update_plan_block_parameters()) {  // Notify stepper module to recompute for hold deceleration.
            sys.step_control.endMotion = true;
        }
        sys.step_control.executeHold = true;  // Initiate suspend state with active flag.
    }
    // log_info("protocol_start_holding");
    // config->_oled->refresh_display();  // Update display to show "Pausing" message
}

static void protocol_cancel_jogging() {
    if (!sys.suspend.bit.motionCancel) {
        sys.suspend.bit.jogCancel = true;
    }
}

static void protocol_hold_complete() {
    sys.suspend.value            = 0;
    sys.suspend.bit.holdComplete = true;
}

static void protocol_do_motion_cancel() {
    // log_debug("protocol_do_motion_cancel " << state_name());
    // Execute and flag a motion cancel with deceleration and return to idle. Used primarily by probing cycle
    // to halt and cancel the remainder of the motion.

    // MOTION_CANCEL only occurs during a CYCLE, but a HOLD and SAFETY_DOOR may have been initiated
    // beforehand. Motion cancel affects only a single planner block motion, while jog cancel
    // will handle and clear multiple planner block motions.
    switch (sys.state) {
        case State::Alarm:
        case State::ConfigAlarm:
        case State::CheckMode:
            return;  // Do not set motionCancel

        case State::Idle:
            protocol_hold_complete();
            break;

        case State::Cycle:
            protocol_start_holding();
            break;

        case State::Jog:
            protocol_start_holding();
            protocol_cancel_jogging();
            // When jogging, we do not set motionCancel, hence return not break
            return;

        case State::Homing:
            // XXX maybe motion cancel should stop homing
        case State::Sleep:
        case State::Hold:
        case State::SafetyDoor:
            break;
    }
    sys.suspend.bit.motionCancel = true;
}

static void protocol_do_feedhold(void *arg) {

    bool sync = (bool)arg;
    
    // Set flag immediately for user feedback (State::Cycle is the internal name for "Run")
    if (sys.state == State::Cycle || sys.state == State::Jog) {
        sys.pauseRequested = true;
    }
    
    log_info("Feedhold process initiated");
    
    // Block feedhold during parking operations only
    if (sys.parkingInProgress) {
        log_info("Feedhold deferred during parking operation");
        sys.deferredPauseRequest = true;
        return;  // Defer feedhold
    }

    // Sync buffers before feedholding if requested
    if (sync) {
        protocol_buffer_synchronize();  // Sync and finish all remaining buffered motions before moving on.
    }

    if (runLimitLoop) {
        runLimitLoop = false;  // Hack to stop show_limits()
        return;
    }

    // log_debug("protocol_do_feedhold " << state_name());
    // Execute a feed hold with deceleration, if required. Then, suspend system.
    switch (sys.state) {
        case State::ConfigAlarm:
        case State::Alarm:
        case State::CheckMode:
        case State::SafetyDoor:
        case State::Sleep:
            return;  // Do not change the state to Hold

        case State::Homing:
            // XXX maybe feedhold should stop homing
            // log_info("Feedhold ignored while homing; use Reset instead");
            return;
        case State::Hold:
            break;

        case State::Idle:
            protocol_hold_complete();
            break;

        case State::Cycle:
            sys.state = State::Hold;  // Set state BEFORE starting deceleration
            config->_oled->refresh_display();  // Immediately update OLED to show "Pausing"
            protocol_start_holding();
            break;

        case State::Jog:
            protocol_start_holding();
            protocol_cancel_jogging();
            return;  // Do not change the state to Hold
    }
    // State::Cycle now sets state above, other cases fall through to here
    if (sys.state != State::Hold) {
        sys.state = State::Hold;
    }
}

static void protocol_do_safety_door() {
    // log_debug("protocol_do_safety_door " << int(sys.state));
    // Execute a safety door stop with a feed hold and disable spindle/coolant.
    // NOTE: Safety door differs from feed holds by stopping everything no matter state, disables powered
    // devices (spindle/coolant), and blocks resuming until switch is re-engaged.

    report_feedback_message(Message::SafetyDoorAjar);
    switch (sys.state) {
        case State::ConfigAlarm:
            return;
        case State::Alarm:
        case State::CheckMode:
        case State::Sleep:
            rtSafetyDoor = false;
            return;  // Do not change the state to SafetyDoor

        case State::Hold:
            break;
        case State::Homing:
            Machine::Homing::fail(ExecAlarm::HomingFailDoor);
            break;
        case State::SafetyDoor:
            if (!sys.suspend.bit.jogCancel && sys.suspend.bit.initiateRestore) {  // Actively restoring
                // Set hold and reset appropriate control flags to restart parking sequence.
                if (sys.step_control.executeSysMotion) {
                    Stepper::update_plan_block_parameters();  // Notify stepper module to recompute for hold deceleration.
                    sys.step_control                  = {};
                    sys.step_control.executeHold      = true;
                    sys.step_control.executeSysMotion = true;
                    sys.suspend.bit.holdComplete      = false;
                }  // else NO_MOTION is active.

                sys.suspend.bit.retractComplete = false;
                sys.suspend.bit.initiateRestore = false;
                sys.suspend.bit.restoreComplete = false;
                sys.suspend.bit.restartRetract  = true;
            }
            break;
        case State::Idle:
            protocol_hold_complete();
            break;
        case State::Cycle:
            protocol_start_holding();
            break;
        case State::Jog:
            protocol_start_holding();
            protocol_cancel_jogging();
            break;
    }
    if (!sys.suspend.bit.jogCancel) {
        // If jogging, leave the safety door event pending until the jog cancel completes
        rtSafetyDoor = false;
        sys.state    = State::SafetyDoor;
    }
    // NOTE: This flag doesn't change when the door closes, unlike sys.state. Ensures any parking motions
    // are executed if the door switch closes and the state returns to HOLD.
    sys.suspend.bit.safetyDoorAjar = true;
}

static void protocol_do_sleep() {
    // log_debug("protocol_do_sleep " << state_name());
    switch (sys.state) {
        case State::ConfigAlarm:
        case State::Alarm:
            sys.suspend.bit.retractComplete = true;
            sys.suspend.bit.holdComplete    = true;
            break;

        case State::Idle:
            protocol_hold_complete();
            break;

        case State::Cycle:
        case State::Jog:
            protocol_start_holding();
            // Unlike other hold events, sleep does not set jogCancel
            break;

        case State::CheckMode:
        case State::Sleep:
        case State::Hold:
        case State::Homing:
        case State::SafetyDoor:
            break;
    }
    sys.state = State::Sleep;
}

void protocol_cancel_disable_steppers() {
    // Cancel any pending stepper disable.
    idleEndTime = 0;
}

static void protocol_do_initiate_cycle() {
    // log_debug("protocol_do_initiate_cycle " << state_name());
    // Start cycle only if queued motions exist in planner buffer and the motion is not canceled.
    sys.step_control = {};  // Restore step control to normal operation
    plan_block_t* pb;
    if ((pb = plan_get_current_block()) && !sys.suspend.bit.motionCancel) {
        sys.suspend.value = 0;  // Break suspend state.
#ifdef ENABLE_WIFI
        WiFi.setAutoReconnect(false);
#endif
        sys.state         = pb->is_jog ? State::Jog : State::Cycle;
        
        // Clear any deferred pause when resuming motion
        if (sys.deferredPauseRequest) {
            log_info("Cleared stale deferred pause on resume to " << state_name());
            sys.deferredPauseRequest = false;
        }

        Stepper::prep_buffer();  // Initialize step segment buffer before beginning cycle.
        Stepper::wake_up();
    } else {  // Otherwise, do nothing. Set and resume IDLE state.

        sys.suspend.value = 0;  // Break suspend state.
        sys.state         = State::Idle;
#ifdef ENABLE_WIFI
        WiFi.setAutoReconnect(true);
        if (WiFi.status() != WL_CONNECTED) {
            WiFi.reconnect();
        }
#endif
    }
}
static void protocol_initiate_homing_cycle() {
    // log_debug("protocol_initiate_homing_cycle " << state_name());
    sys.step_control                  = {};    // Restore step control to normal operation
    sys.suspend.value                 = 0;     // Break suspend state.
    sys.step_control.executeSysMotion = true;  // Set to execute homing motion and clear existing flags.
    Stepper::prep_buffer();                    // Initialize step segment buffer before beginning cycle.
    Stepper::wake_up();
}

static void protocol_do_cycle_start() {
    sys.pauseRequested = false;  // Clear when resuming
    // log_debug("protocol_do_cycle_start " << state_name());
    // Execute a cycle start by starting the stepper interrupt to begin executing the blocks in queue.

    // Resume door state when parking motion has retracted and door has been closed.
    switch (sys.state) {
        case State::SafetyDoor:
            if (!sys.suspend.bit.safetyDoorAjar) {
                if (sys.suspend.bit.restoreComplete) {
                    sys.state = State::Idle;
                    protocol_do_initiate_cycle();
                } else if (sys.suspend.bit.retractComplete) {
                    sys.suspend.bit.initiateRestore = true;
                }
            }
            break;
        case State::Idle:
            protocol_do_initiate_cycle();
            break;
        case State::Homing:
            protocol_initiate_homing_cycle();
            break;
        case State::Hold:
            // Cycle start only when IDLE or when a hold is complete and ready to resume.
            if (sys.suspend.bit.holdComplete) {
                if (spindle_stop_ovr.value) {
                    spindle_stop_ovr.bit.restoreCycle = true;  // Set to restore in suspend routine and cycle start after.
                } else {
                    // Unpark before resuming if needed
                    if ((config->_parking->park_on_feedhold()) && (sys.suspend.bit.retractComplete)) {
                        // Clear M0 comment when initiating restore (parking path)
                        if (config && config->_oled) {
                            config->_oled->clear_m0_comment();
                        }
                        sys.suspend.bit.initiateRestore = true;
                        // Force display refresh to show "Resuming..." before unpark blocks
                        if (config && config->_oled) {
                            config->_oled->refresh_display();
                        }
                    // Otherwise, resume
                    } else {
                        // Clear M0 comment when resuming
                        if (config && config->_oled) {
                            config->_oled->clear_m0_comment();
                        }
                        protocol_do_initiate_cycle();
                    }
                }
            }
            break;
        case State::ConfigAlarm:
        case State::Alarm:
        case State::CheckMode:
        case State::Sleep:
        case State::Cycle:
        case State::Jog:
            break;
    }
}

void protocol_disable_steppers() {
    if (sys.state == State::Homing) {
        // Leave steppers enabled while homing
        config->_axes->set_disable(false);
        return;
    }
    if (sys.state == State::Sleep || rtAlarm != ExecAlarm::None) {
        // Disable steppers immediately in sleep or alarm state
        config->_axes->set_disable(true);
        return;
    }
    if (config->_stepping->_idleMsecs == 255) {
        // Leave steppers enabled if configured for "stay enabled"
        config->_axes->set_disable(false);
        return;
    }
    // Otherwise, schedule stepper disable in a few milliseconds
    // unless a disable time has already been scheduled
    if (idleEndTime == 0) {
        idleEndTime = usToEndTicks(config->_stepping->_idleMsecs * 1000);
        // idleEndTime 0 means that a stepper disable is not scheduled. so if we happen to
        // land on 0 as an end time, just push it back by one microsecond to get off 0.
        if (idleEndTime == 0) {
            idleEndTime = 1;
        }
    }
}

void protocol_do_cycle_stop() {
    // log_debug("protocol_do_cycle_stop " << state_name());
    protocol_disable_steppers();

    switch (sys.state) {
        case State::Hold:
        case State::SafetyDoor:
        case State::Sleep:
            // Reinitializes the cycle plan and stepper system after a feed hold for a resume. Called by
            // realtime command execution in the main program, ensuring that the planner re-plans safely.
            // NOTE: Bresenham algorithm variables are still maintained through both the planner and stepper
            // cycle reinitializations. The stepper path should continue exactly as if nothing has happened.
            // NOTE: cycleStopEvent is set by the stepper subsystem when a cycle or feed hold completes.
            if (!soft_limit && !sys.suspend.bit.jogCancel) {
                // Hold complete. Set to indicate ready to resume.  Remain in HOLD or DOOR states until user
                // has issued a resume command or reset.
                plan_cycle_reinitialize();
if (sys.step_control.executeHold) {
                    sys.suspend.bit.holdComplete = true;
                    // Force status report to update OLED with Hold:0 state
                    report_realtime_status(allChannels);
                } else {
                    // This is likely parking motion completing - send status report to update OLED
                    if (sys.state == State::Hold && sys.suspend.bit.holdComplete) {
                        report_realtime_status(allChannels);
                    }
                }
                sys.step_control.executeHold      = false;
                sys.step_control.executeSysMotion = false;
                break;
            }
            // Fall through
        case State::ConfigAlarm:
        case State::Alarm:
        case State::CheckMode:
        case State::Idle:
        case State::Cycle:
        case State::Jog:
            // Motion complete. Includes CYCLE/JOG/HOMING states and jog cancel/motion cancel/soft limit events.
            // NOTE: Motion and jog cancel both immediately return to idle after the hold completes.
            if (sys.suspend.bit.jogCancel) {  // For jog cancel, flush buffers and sync positions.
                sys.step_control = {};
                plan_reset();
                Stepper::reset();
                gc_sync_position();
                plan_sync_position();
            }
            if (sys.suspend.bit.safetyDoorAjar) {  // Only occurs when safety door opens during jog.
                sys.suspend.bit.jogCancel    = false;
                sys.suspend.bit.holdComplete = true;
                sys.state                    = State::SafetyDoor;
            } else {
                sys.suspend.value = 0;
                sys.state         = State::Idle;
#ifdef ENABLE_WIFI
                WiFi.setAutoReconnect(true);
                if (WiFi.status() != WL_CONNECTED) {
                    WiFi.reconnect();
                }
#endif
            }
            break;
        case State::Homing:
            Machine::Homing::cycleStop();
            break;
    }

    // log_debug("End Cycle Stop");
    config->_oled->refresh_display(); // AIDAN
}

static void update_velocities() {
    report_ovr_counter = 0;  // Set to report change immediately
    plan_update_velocity_profile_parameters();
    plan_cycle_reinitialize();
}

// This is the final phase of the shutdown activity that is initiated by mc_reset().
// The stuff herein is not necessarily safe to do in an ISR.
static void protocol_do_late_reset() {
    // Kill spindle and coolant.
    spindle->stop();
    report_ovr_counter = 0;  // Set to report change immediately
    config->_coolant->stop();

    protocol_disable_steppers();
    config->_stepping->reset();

    // turn off all User I/O immediately
    config->_userOutputs->all_off();

    // Restore per-axis config defaults on abort/alarm/soft-reset so an in-job
    // g-code comment override (Media* soft limits, (Accel) acceleration) does
    // not leak into the next attempt.
    config->_axes->restoreJobDefaults();

    // do we need to stop a running file job?
    allChannels.stopJob();
}

void protocol_exec_rt_system() {
    protocol_do_alarm();  // If there is a hard or soft limit, this will block until rtReset is set

    if (rtReset) {
        rtReset = false;
        if (sys.state == State::Homing) {
            Machine::Homing::fail(ExecAlarm::HomingFailReset);
        }
        protocol_do_late_reset();
        // Trigger system abort.
        sys.abort = true;  // Only place this is set true.
        return;            // Nothing else to do but exit.
    }

    if (rtSafetyDoor) {
        protocol_do_safety_door();
    }

    protocol_handle_events();

    // Reload step segment buffer
    switch (sys.state) {
        case State::ConfigAlarm:
        case State::Alarm:
        case State::CheckMode:
        case State::Idle:
        case State::Sleep:
            break;
        case State::Cycle:
        case State::Hold:
        case State::SafetyDoor:
        case State::Homing:
        case State::Jog:
            Stepper::prep_buffer();
            break;
    }
}

static void protocol_manage_spindle() {
    // Feed hold manager. Controls spindle stop override states.
    // NOTE: Hold ensured as completed by condition check at the beginning of suspend routine.
    if (spindle_stop_ovr.value) {
        // Handles beginning of spindle stop
        if (spindle_stop_ovr.bit.initiate) {
            if (gc_state.modal.spindle != SpindleState::Disable) {
                spindle->spinDown();
                report_ovr_counter           = 0;  // Set to report change immediately
                spindle_stop_ovr.value       = 0;
                spindle_stop_ovr.bit.enabled = true;  // Set stop override state to enabled, if de-energized.
            } else {
                spindle_stop_ovr.value = 0;  // Clear stop override state
            }
            // Handles restoring of spindle state
        } else if (spindle_stop_ovr.bit.restore || spindle_stop_ovr.bit.restoreCycle) {
            if (gc_state.modal.spindle != SpindleState::Disable) {
                report_feedback_message(Message::SpindleRestore);
                if (spindle->isRateAdjusted()) {
                    // When in laser mode, defer turn on until cycle starts
                    sys.step_control.updateSpindleSpeed = true;
                } else {
                    config->_parking->restore_spindle();
                    report_ovr_counter = 0;  // Set to report change immediately
                }
            }
            if (spindle_stop_ovr.bit.restoreCycle) {
                protocol_send_event(&cycleStartEvent);  // Resume program.
            }
            spindle_stop_ovr.value = 0;  // Clear stop override state
        }
    } else {
        // Handles spindle state during hold. NOTE: Spindle speed overrides may be altered during hold state.
        // NOTE: sys.step_control.updateSpindleSpeed is automatically reset upon resume in step generator.
        if (sys.step_control.updateSpindleSpeed) {
            config->_parking->restore_spindle();
            sys.step_control.updateSpindleSpeed = false;
        }
    }
}

// Handles system suspend procedures, such as feed hold, safety door, and parking motion.
// The system will enter this loop, create local variables for suspend tasks, and return to
// whatever function that invoked the suspend, resuming normal operation.
static void protocol_exec_rt_suspend() {
    config->_parking->setup();

    if (spindle->isRateAdjusted()) {
        protocol_send_event(&accessoryOverrideEvent, (void*)AccessoryOverride::SpindleStopOvr);
    }

    while (sys.suspend.value) {
        if (sys.abort) {
            return;
        }
        // if a jogCancel comes in and we have a jog "in-flight" (parsed and handed over to mc_move_motors()),
        //  then we need to cancel it before it reaches the planner.  otherwise we may try to move way out of
        //  normal bounds, especially with senders that issue a series of jog commands before sending a cancel.
        if (sys.suspend.bit.jogCancel) {
            mc_cancel_jog();
        }
        // Block until initial hold is complete and the machine has stopped motion.
        if (sys.suspend.bit.holdComplete) {
            // Parking manager. Handles de/re-energizing, switch state checks, and parking motions for
            // the safety door, sleep and hold states (if enabled).
            if (sys.state == State::SafetyDoor || sys.state == State::Sleep || (sys.state == State::Hold && config->_parking->park_on_feedhold())) {
                // Handles retraction motions and de-energizing.
                config->_parking->set_target();
                if (!sys.suspend.bit.retractComplete) {
                    // Ensure any prior spindle stop override is disabled at start of safety door routine.
                    spindle_stop_ovr.value = 0;  // Disable override

                    // Execute slow pull-out parking retract motion. Parking requires homing enabled, the
                    // current location not exceeding the parking target location, and laser mode disabled.
                    // NOTE: State will remain DOOR, until the de-energizing and retract is complete.
                    config->_parking->park(sys.suspend.bit.restartRetract);

                    sys.suspend.bit.retractComplete = true;
                    sys.suspend.bit.restartRetract  = false;

                    // Send status report to update OLED after parking completes
                    report_realtime_status(allChannels);

                    if (config->_control->enter_locked()) {
                        config->_control->unlock_enter();
                    }
                } else {
                    if (sys.state == State::Sleep) {
                        report_feedback_message(Message::SleepMode);
                        // Spindle and coolant should already be stopped, but do it again just to be sure.
                        spindle->spinDown();
                        config->_coolant->off();
                        report_ovr_counter = 0;  // Set to report change immediately
                        Stepper::go_idle();      // Stop stepping and maybe disable steppers
                        while (!(sys.abort)) {
                            protocol_exec_rt_system();  // Do nothing until reset.
                        }
                        return;  // Abort received. Return to re-initialize.
                    }
                    // Allows resuming from parking/safety door. Polls to see if safety door is closed and ready to resume.
                    if (sys.state == State::SafetyDoor && !config->_control->safety_door_ajar()) {
                        if (sys.suspend.bit.safetyDoorAjar) {
                            log_info("Safety door closed.  Issue cycle start to resume");
                        }
                        sys.suspend.bit.safetyDoorAjar = false;  // Reset door ajar flag to denote ready to resume.
                    }
                    if (sys.suspend.bit.initiateRestore) {
                        config->_parking->unpark(sys.suspend.bit.restartRetract);
                        
                        // Clear flags for park on feedhold
                        if (config->_parking->park_on_feedhold()) {
                            sys.suspend.bit.initiateRestore = false;
                            sys.suspend.bit.retractComplete = false;
                        }

                        if (!sys.suspend.bit.restartRetract && 
                            ((sys.state == State::SafetyDoor && !sys.suspend.bit.safetyDoorAjar) ||
                             (sys.state == State::Hold && config->_parking->park_on_feedhold()))) {
                            // Check deferred pause BEFORE resuming to prevent motion corruption
                            if (sys.deferredPauseRequest) {
                                log_info("Deferred pause after unparking - re-parking immediately");
                                sys.deferredPauseRequest = false;
                                
                                // Re-park with original position preserved
                                config->_parking->park(true);  // true = keep original restore position
                                
                                // Stay in Hold:0 - no state change, no resume
                            } else {
                                // Only resume if no deferred pause
                                sys.state = State::Idle;
                                protocol_send_event(&cycleStartEvent);  // Resume program
                            }
                        }
                    }
                }
            } else {
                protocol_manage_spindle();
                if (!config->_parking->park_on_feedhold() && config->_control->enter_locked()) {
                    config->_control->unlock_enter();   // Unlock enter button once hold complete
                }
            }
        }
        protocol_exec_rt_system();
    }
}

static void protocol_do_feed_override(void* incrementvp) {
    int increment = int(incrementvp);
    int percent;
    if (increment == FeedOverride::Default) {
        percent = FeedOverride::Default;
    } else {
        percent = sys.f_override + increment;
        if (percent > FeedOverride::Max) {
            percent = FeedOverride::Max;
        } else if (percent < FeedOverride::Min) {
            percent = FeedOverride::Min;
        }
    }
    if (percent != sys.f_override) {
        sys.f_override = percent;
        update_velocities();
    }
}

static void protocol_do_rapid_override(void* percentvp) {
    int percent = int(percentvp);
    if (percent != sys.r_override) {
        sys.r_override = percent;
        update_velocities();
    }
}

static void protocol_do_spindle_override(void* incrementvp) {
    int percent;
    int increment = int(incrementvp);
    if (increment == SpindleSpeedOverride::Default) {
        percent = SpindleSpeedOverride::Default;
    } else {
        percent = sys.spindle_speed_ovr + increment;
        if (percent > SpindleSpeedOverride::Max) {
            percent = SpindleSpeedOverride::Max;
        } else if (percent < SpindleSpeedOverride::Min) {
            percent = SpindleSpeedOverride::Min;
        }
    }
    if (percent != sys.spindle_speed_ovr) {
        sys.spindle_speed_ovr               = percent;
        sys.step_control.updateSpindleSpeed = true;
        report_ovr_counter                  = 0;  // Set to report change immediately

        // If spindle is on, tell it the RPM has been overridden
        // When moving, the override is handled by the stepping code
        if (gc_state.modal.spindle != SpindleState::Disable && !inMotionState()) {
            spindle->setState(gc_state.modal.spindle, gc_state.spindle_speed);
            report_ovr_counter = 0;  // Set to report change immediately
        }
    }
}

static void protocol_do_accessory_override(void* type) {
    switch (int(type)) {
        case AccessoryOverride::SpindleStopOvr:
            // Spindle stop override allowed only while in HOLD state.
            if (sys.state == State::Hold) {
                if (spindle_stop_ovr.value == 0) {
                    spindle_stop_ovr.bit.initiate = true;
                } else if (spindle_stop_ovr.bit.enabled) {
                    spindle_stop_ovr.bit.restore = true;
                }
                report_ovr_counter = 0;  // Set to report change immediately
            }
            break;
        case AccessoryOverride::FloodToggle:
            // NOTE: Since coolant state always performs a planner sync whenever it changes, the current
            // run state can be determined by checking the parser state.
            if (config->_coolant->hasFlood() && (sys.state == State::Idle || sys.state == State::Cycle || sys.state == State::Hold)) {
                gc_state.modal.coolant.Flood = !gc_state.modal.coolant.Flood;
                config->_coolant->set_state(gc_state.modal.coolant);
                report_ovr_counter = 0;  // Set to report change immediately
            }
            break;
        case AccessoryOverride::MistToggle:
            if (config->_coolant->hasMist() && (sys.state == State::Idle || sys.state == State::Cycle || sys.state == State::Hold)) {
                gc_state.modal.coolant.Mist = !gc_state.modal.coolant.Mist;
                config->_coolant->set_state(gc_state.modal.coolant);
                report_ovr_counter = 0;  // Set to report change immediately
            }
            break;
        default:
            break;
    }
}

static void protocol_do_limit(void* arg) {
    Machine::LimitPin* limit = (Machine::LimitPin*)arg;
    if (sys.state == State::Homing) {
        Machine::Homing::limitReached();
        return;
    }
    //log_debug("Limit switch tripped for " << config->_axes->axisName(limit->_axis) << " motor " << limit->_motorNum);
    if (sys.state == State::Cycle || sys.state == State::Jog) {
        if (limit->isHard() && rtAlarm == ExecAlarm::None) {
            log_debug("Hard limits");
            mc_reset();                      // Initiate system kill.
            rtAlarm = ExecAlarm::HardLimit;  // Indicate hard limit critical event
        }
        return;
    }
}

static void protocol_do_card_detect(void* arg) {

    // Do nothing, use update() in CardDetectPin instead
}

// Launches an SD gcode file by base-relative path. Strips any completion prefix,
// constructs the InputFile, and registers it. Reports open failures via popups.
// Does NOT set the completed-file path or home — callers own those.
static void launch_sd_file(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        config->_oled->popup_msg("Previously run file not found");
        log_info("launch_sd_file: empty path");
        return;
    }
    std::string stripped_storage;
    const char* path_to_open = CompletionMark::resolve_with_strip(path, stripped_storage);
    log_info("launch_sd_file: " << path_to_open);
    try {
        InputFile* infile = new InputFile(
            "sd", path_to_open, WebUI::AuthenticationLevel::LEVEL_ADMIN, allChannels);
        allChannels.registration(infile);
    } catch (const std::filesystem::filesystem_error& e) {
        log_error("Cannot open SD file: " << e.what());
        config->_oled->popup_msg("microSD card not\ndetected", 3000);
    } catch (Error) {
        log_error("Cannot open SD file: " << path_to_open);
        config->_oled->popup_msg("Cannot open\nSD file", 3000);
    }
}

static void protocol_do_enter() {
    log_info("Button press detected");

    // Drop any pending encoder rotation accumulated by mechanical
    // wobble at click time — otherwise the polling task's next
    // show_menu() pass consumes the phantom pulse and briefly
    // shifts the cursor before any action handler's own rendering
    // takes over (visible as a "menu flash" before WiFi-toggle
    // popups). Same reasoning as the _enc_diff clear already in
    // refresh_display(menu_only=true) per , just applied here
    // at click-detection time so it covers all action handlers.
    if (config && config->_oled) {
        config->_oled->drop_pending_encoder();
    }

    // Check if parking is in progress
    if (sys.parkingInProgress) {
        log_info("Button dismissed during parking operation");
        if (sys.state == State::Cycle) {
            sys.deferredPauseRequest = true;
            log_info("Pause request deferred until after parking completes");
        }
        return;  // Dismiss button press
    }

    // BusyScreen: ignore button presses while busy
    if (config->_oled && config->_oled->isBusy()) {
        return;
    }

    // Bail if enter button locked out (prevents duplicates)
    if (config->_control->enter_locked()) {
        log_info("Enter Locked, exiting protocol");
        return;
    }

    if (sys.state == State::Cycle) {
        
        // Normal user pause - no M0 pending
        config->_control->lock_enter();  // Prevent duplicate feedhold events
        sys.pauseRequested = true;        // Set flag for immediate feedback
        protocol_send_event(&feedHoldEvent, true); // True -> Exhaust queue, do not decelerate
        log_info("Pause requested during cycle");
        return;
    }
    
    if (sys.state == State::Hold && !sys.suspend.bit.holdComplete) {
        // Hold:1 (still decelerating) - ignore button
        log_info("Button pressed during deceleration - ignored");
        return;
    }
    
    if (sys.state == State::Hold && sys.suspend.bit.holdComplete) {
        // Hold:0 - machine stopped, safe to block
        // If we're here, button was definitely released after pause (edge-triggered)
        log_info("Button pressed in Hold:0 - checking for long press");
        
        // Check if parking is in progress - CRITICAL FIX
        if (sys.parkingInProgress) {
            log_info("Button dismissed during parking operation (Hold:0 state)");
            sys.deferredPauseRequest = true;
            log_info("Resume request deferred until after parking completes");
            return;  // Dismiss button press
        }
        
        uint32_t threshold = millis() + config->_control->_long_press_ms;
        
        // Block while button held (safe - machine is stopped)
        while (config->_control->enter_pressed() && millis() < threshold) {
            delay_ms(10);
        }
        
        if (millis() >= threshold) {
            // Long press - Cancel
            log_info("Cancel requested during hold");
            protocol_send_event(&resetEvent);
        } else {
            // Short press - Resume
            log_info("Resume requested during hold");
            config->_control->lock_enter();
            protocol_send_event(&cycleStartEvent);
            protocol_execute_realtime();
            if (config->_control->enter_locked()) {
                config->_control->unlock_enter();
            }
        }
        return;
    }
    
    // Handle other states with existing switch statement
    switch (sys.state) {
        case State::ConfigAlarm:
            // Handle recovery config loading
            if (config && config->_oled && config->_oled->showing_popup()) {
                copyRecoveryConfigAndRestart();
                // Function will either restart or show error popup
            }
            break;

        // Button press does nothing in these states
        case State::CheckMode:
        case State::Jog:
        case State::Homing:
        case State::Sleep:
        case State::SafetyDoor:
            break;

        // Clear alarm when in ALARM state
        case State::Alarm:
            config->_control->lock_enter();
            // log_debug("Alarm State in do_enter");
            sys.state = State::Idle;
            
            protocol_send_event(&resetEvent);
            if (config->_control->enter_locked()) {
                config->_control->unlock_enter();
            }
            // config->_oled->refresh_display();

            break;

        // Run selected operation when IDLE
        case State::Idle:
            // log_debug("Idle State in do_enter");

            // Be sure display is enabled
            if (config->_oled) {

                // Double-check to make sure we are not currently in the middle of running a file.
                // This shouldn't normally happen, but we've seen the system state flash to "IDLE" in
                //  between Run and Hold when transitioning to and from a paused state.
                // We don't want to be trying to do anything with menu entries during file run, so bail.
                if (config->_oled->is_file_job_running()) {
                    log_debug("do_enter saw Idle state with file running - bailing.");
                    break;
                }

                if (config->_oled->showing_popup()) {
                    // click while popup is displayed -> clear popup, don't do anything else
                    config->_oled->clear_popup();
                    break;
                }
            
                if (config->_oled->_menu->sd_browse_active()) {
                    sdfiles::EntryId outFile = sdfiles::kInvalidEntry;
                    // Hold the arena lock from id-resolution (activate) THROUGH the
                    // copyName/fullPath copy-out below, so a concurrent poller mutation
                    // cannot invalidate outFile between resolve and read. Released in each
                    // branch before the heavy work (flash / run / popup / I2C).
                    std::unique_lock<std::recursive_mutex> arenaLock(
                        config->_oled->_menu->sd_table().mutex());
                    sdfiles::SDBrowser::Activation act =
                        config->_oled->_menu->sd_browser().activate(
                            config->_oled->_menu->sd_table(), outFile);
                    if (act == sdfiles::SDBrowser::Activation::ExitedToMain) {
                        arenaLock.unlock();
                        config->_oled->_menu->set_sd_browse_active(false);
                        config->_oled->_menu->exit_submenu();
                    } else if (act == sdfiles::SDBrowser::Activation::EnteredDir ||
                               act == sdfiles::SDBrowser::Activation::ExitedToParent) {
                        arenaLock.unlock();
                        config->_oled->refresh_display();
                    } else if (act == sdfiles::SDBrowser::Activation::SelectedFile) {
                        sdfiles::FileClass cls = config->_oled->_menu->sd_browser().classFilter();
                        if (cls == sdfiles::FileClass::Firmware) {
                            char nm[sdfiles::kMaxNameLen + 1];
                            config->_oled->_menu->sd_table().copyName(outFile, nm, sizeof(nm));
                            arenaLock.unlock();
                            std::string fw_file = nm;
                            log_info("Selected: " << fw_file);
                            config->_oled->popup_msg("Updating Firmware\nPlotter will restart...", 0);
                            Flashing::update_firmware_from_sdcard(fw_file);
                        } else if (cls == sdfiles::FileClass::Config) {
                            char nm[sdfiles::kMaxNameLen + 1];
                            config->_oled->_menu->sd_table().copyName(outFile, nm, sizeof(nm));
                            arenaLock.unlock();
                            std::string cfg_file = nm;
                            log_info("Config Selected: " << cfg_file);
                            config->_oled->popup_msg("Updating Config File\nPlotter will restart...", 0);
                            Flashing::update_config_from_sdcard(cfg_file, true);
                        } else {
                            char pathbuf[LIST_NAME_MAX_PATH];
                            bool path_ok = config->_oled->_menu->sd_table().fullPath(
                                outFile, pathbuf, sizeof(pathbuf), /*includeCompletionMark=*/true);
                            arenaLock.unlock();
                            if (!path_ok) {
                                config->_oled->popup_msg("Path too long to run", 0);
                            } else {
                                std::string stripped_storage;
                                const char* path_to_open =
                                    CompletionMark::resolve_with_strip(pathbuf, stripped_storage);
                                if (!config->_axes->_homed && config->_kinematics->canHome(0) &&
                                        !(config->getMachineType() == Machine::MachineType::EggBot)) {
                                    log_info("Unhomed. About to home before running file: " << path_to_open);
                                    config->_oled->set_file_awaiting_homing(path_to_open);
                                    config->_oled->popup_msg("Homing before file run...", 0);
                                    Machine::Homing::run_cycles(Machine::Homing::AllCycles);
                                } else {
                                    log_info("Passing path to InputFile: " << path_to_open);
                                    config->_oled->_menu->set_completed_file(path_to_open);
                                    try {
                                        InputFile* infile = new InputFile(
                                            "sd", path_to_open, WebUI::AuthenticationLevel::LEVEL_ADMIN, allChannels);
                                        allChannels.registration(infile);
                                    } catch (const std::filesystem::filesystem_error& e) {
                                        log_error("Cannot open SD file: " << e.what());
                                        config->_oled->popup_msg("microSD card not\ndetected", 3000);
                                    } catch (Error) {
                                        log_error("Cannot open SD file: " << path_to_open);
                                        config->_oled->popup_msg("Cannot open\nSD file", 3000);
                                    }
                                }
                            }
                        }
                    }
                    // act == None: nothing to do.
                } else if (strcmp(config->_oled->_menu->get_selected()->display_name, "Home") == 0) {
                    if (config->getMachineType() == Machine::MachineType::EggBot) { // motor power toggle for EggBot only
                        // Clear calibration state when toggling motor lock
                        clearRcServoCalibration();

                        bool currently_disabled = config->_axes->motors_are_disabled();
                        config->_axes->set_disable(!currently_disabled);
                        config->_oled->refresh_display();
                    } else { // all other machines do homing cycle
                        Machine::Homing::run_cycles(Machine::Homing::AllCycles);
                        
                        // Wait for homing to complete
                        do {
                            protocol_execute_realtime();
                        } while (sys.state == State::Homing);
                        
                        // Execute startup scripts after homing, matching $H behavior
                        settings_execute_startup();
                    }

                // Jog command (Jog X, Jog Y, Jog Z - single letter after "Jog ")
                } else if (strlen(config->_oled->_menu->get_selected()->display_name) == 5 &&
                          strstr(config->_oled->_menu->get_selected()->display_name, "Jog ")) {

                    // Check if machine needs homing
                    if (!config->_axes->_homed && config->_axes->hasRealHomingCycles()) {
                        // Show homing choice menu (acts as modal popup over jogging menu)
                        config->_oled->_menu->go_to_homing_choice_menu();
                        
                    } else {

                        char axis = (strrchr(config->_oled->_menu->get_selected()->display_name, ' ') + 1)[0];

                        // Enter jogging mode if not active
                        if (config->_oled->get_jog_state() == JogState::Idle) {
                            config->_oled->set_jog_state(JogState::Scrolling);

                        // Exit jogging mode if press 
                        } else {
                            config->_oled->set_jog_state(JogState::Idle);
                        }
                    }

                // Factory Reset command
                } else if (strcmp(config->_oled->_menu->get_selected()->display_name, "Confirm Factory Reset") == 0) {
                    //config->_oled->popup_msg("Factory Reset Test!");
                    // Restore settings to defaults
                    settings_restore(SettingsRestore::Wifi | SettingsRestore::Defaults | SettingsRestore::StartupLines | SettingsRestore::Parameters);

                    // Restart when done
                    ESP.restart();
                    while (1) {}
                } else if (strcmp(config->_oled->_menu->get_selected()->display_name, "Cancel Factory Reset") == 0) {
                    // Just treat this like a back button
                    config->_oled->_menu->exit_submenu();

                // Handle homing choice menu
                } else if (config->_oled->_menu->is_homing_choice_menu()) {
                    
                    if (strcmp(config->_oled->_menu->get_selected()->display_name, BACK_LABEL) == 0) {
                        // Return to jogging menu (parent set during menu initialization)
                        config->_oled->_menu->exit_submenu();
                        // Force clean redraw of jogging menu
                        config->_oled->clear();
                        config->_oled->refresh_display();
                        
                    } else if (strcmp(config->_oled->_menu->get_selected()->display_name, "Run Homing") == 0) {
                        // Display "Homing..." message
                        config->_oled->popup_msg("Homing...", 0);
                        
                        // Start homing and block until complete
                        Machine::Homing::run_cycles(Machine::Homing::AllCycles);
                        do {
                            protocol_execute_realtime();
                        } while (sys.state == State::Homing);
                        
                        // Clear message and return to jogging menu
                        config->_oled->clear_popup();
                        config->_oled->_menu->exit_submenu();
                        // Force clean redraw of jogging menu
                        config->_oled->clear();
                        config->_oled->refresh_display();
                    }
                
                // Wifi commands
                } else if (strcmp(config->_oled->_menu->get_selected()->display_name, "Turn WiFi ON") == 0) {
                    // Only reachable when config->_wifiMode == -1 (user control)
                    // Persist the restored mode and reboot. In-session
                    // wifi_config.begin() exercises the bring-up leak path
                    // documented in ; rebooting brings WiFi up with a
                    // fresh heap.
                    const char* restoreMode = "STA>AP";  // default
                    switch (WebUI::wifi_on_mode->get()) {
                        case 1: restoreMode = "STA"; break;
                        case 2: restoreMode = "AP"; break;
                        case 3: restoreMode = "STA>AP"; break;
                    }
                    WebUI::wifi_mode->setStringValue((char*)restoreMode);
                    // Suspend the polling task before touching the OLED.
                    // The polling task runs on a different core
                    // (SUPPORT_TASK_CORE) and may concurrently call
                    // performDisplayUpdate() — without locks on the
                    // buffer or the I²C bus, two simultaneous I²C
                    // transmissions would interleave and garble the
                    // OLED. Suspending here gives the protocol task
                    // exclusive ownership of the buffer write + flush
                    // for the brief reboot window. No vTaskResume —
                    // we're rebooting, the task table is discarded.
                    vTaskSuspend(pollingTask);
                    config->_oled->popup_msg("Restarting to enable WiFi", 0);
                    // Force the OLED buffer to the screen synchronously
                    // before reboot. SSD1306_I2C::display() only sets a
                    // dirty flag; the actual I²C push happens later in
                    // performDisplayUpdate(), called from the (now
                    // suspended) polling task. Flush here ourselves.
                    {
                        SSD1306_I2C* ssd1306 = static_cast<SSD1306_I2C*>(config->_oled->_oled);
                        if (ssd1306) {
                            ssd1306->performDisplayUpdate();
                        }
                    }
                    ESP.restart();
                    while (1) {}
                } else if (strcmp(config->_oled->_menu->get_selected()->display_name, "Turn WiFi OFF") == 0) {
                    // Save current mode for restore-on-toggle, if different
                    {
                        int8_t activeMode = (config->_wifiMode >= 0) ? config->_wifiMode : WebUI::wifi_mode->get();
                        if (activeMode != WebUI::wifi_on_mode->get()) {
                            const char* modeStr = "STA>AP";  // default
                            switch (activeMode) {
                                case 1: modeStr = "STA"; break;
                                case 2: modeStr = "AP"; break;
                                case 3: modeStr = "STA>AP"; break;
                            }
                            WebUI::wifi_on_mode->setStringValue((char*)modeStr);
                        }
                    }
                    WebUI::wifi_config.end();  // calls StopWiFi()

                    // Suspend the polling task while we modify menu data
                    // and write the popup buffer. The polling task runs on
                    // a different core (SUPPORT_TASK_CORE) and can race
                    // with rebuild_settings_menu (reading the doubly-linked
                    // list mid-modification) and popup_msg (writing the
                    // OLED buffer concurrently with show_menu). Same race
                    // protection the reboot branches use; paired with
                    // resume since we're not rebooting.
                    vTaskSuspend(pollingTask);
                    if (config->_wifiMode == -1) {
                        // User control: persist to NVS
                        WebUI::wifi_mode->setStringValue((char*)"Off");
                    }
                    if (config->_oled && config->_oled->_menu) {
                        config->_oled->_menu->rebuild_settings_menu();
                    }
                    // popup_msg shown after rebuild so it returns to the updated menu
                    if (config->_wifiMode == -1) {
                        config->_oled->popup_msg("WiFi off");
                    } else {
                        config->_oled->popup_msg("WiFi off until restart");
                    }
                    vTaskResume(pollingTask);
                } else if (strcmp(config->_oled->_menu->get_selected()->display_name, "WiFi: Reboot to enable") == 0) {
                    // Suspend the polling task and flush synchronously
                    // (see comment in "Turn WiFi ON" branch above).
                    vTaskSuspend(pollingTask);
                    config->_oled->popup_msg("Now Rebooting...", 0);
                    {
                        SSD1306_I2C* ssd1306 = static_cast<SSD1306_I2C*>(config->_oled->_oled);
                        if (ssd1306) {
                            ssd1306->performDisplayUpdate();
                        }
                    }
                    ESP.restart();
                    while (1) {}
                // : completed-file marking toggle. Two labels, one per state.
                // Update the just-clicked entry's display_name in place rather than
                // calling rebuild_settings_menu() — the rebuild path resets selection
                // to position 0 and doesn't trigger an OLED redraw, so the displayed
                // menu stays stale until the encoder turn forces a refresh, at which
                // point it visibly jumps to the top of the list.
                } else if (strcmp(config->_oled->_menu->get_selected()->display_name,
                                  "\xE2\x9C\x93" " Completed files: ON") == 0) {
                    completion_marking->setStringValue((char*)"OFF");
                    ListNodeType* sel = config->_oled->_menu->get_selected();
                    free(sel->display_name);
                    sel->display_name = strdup("\xE2\x9C\x93" " Completed files: OFF");
                    config->_oled->refresh_display(true);  // menu-only redraw
                } else if (strcmp(config->_oled->_menu->get_selected()->display_name,
                                  "\xE2\x9C\x93" " Completed files: OFF") == 0) {
                    completion_marking->setStringValue((char*)"ON");
                    ListNodeType* sel = config->_oled->_menu->get_selected();
                    free(sel->display_name);
                    sel->display_name = strdup("\xE2\x9C\x93" " Completed files: ON");
                    config->_oled->refresh_display(true);  // menu-only redraw
                // : Next File Ordering submenu taps. Each option
                // appears under one of two display_name labels (chosen
                // "✓ Foo" vs. unchosen "   Foo" with 3-space pad);
                // the dispatch matches both forms. 3-space pad chosen
                // to x-align with the chosen form's left edge on the
                // OLED's proportional font. Strcmp targets must match
                // the labels in Menu.cpp exactly or the dispatch
                // fails to fire.
                } else if (strcmp(config->_oled->_menu->get_selected()->display_name, "   Oldest") == 0
                        || strcmp(config->_oled->_menu->get_selected()->display_name, "\xE2\x9C\x93 Oldest") == 0) {
                    config->_oled->_menu->set_next_file_ordering_index(0);
                } else if (strcmp(config->_oled->_menu->get_selected()->display_name, "   Newest") == 0
                        || strcmp(config->_oled->_menu->get_selected()->display_name, "\xE2\x9C\x93 Newest") == 0) {
                    config->_oled->_menu->set_next_file_ordering_index(1);
                } else if (strcmp(config->_oled->_menu->get_selected()->display_name, "   A \xE2\x86\x92 Z") == 0
                        || strcmp(config->_oled->_menu->get_selected()->display_name, "\xE2\x9C\x93 A \xE2\x86\x92 Z") == 0) {
                    config->_oled->_menu->set_next_file_ordering_index(2);
                } else if (strcmp(config->_oled->_menu->get_selected()->display_name, "   Z \xE2\x86\x92 A") == 0
                        || strcmp(config->_oled->_menu->get_selected()->display_name, "\xE2\x9C\x93 Z \xE2\x86\x92 A") == 0) {
                    config->_oled->_menu->set_next_file_ordering_index(3);
                // temp testing
                } else if (strcmp(config->_oled->_menu->get_selected()->display_name, "TEST") == 0) {

                    // draw some stuff on the right side of the menu area
                    //  note this kind of drawing would normally be done within OLED.cpp, not here.
                //    config->_oled->_oled->setColor(BLACK);
                //    config->_oled->_oled->fillRect(64, 15, 64, 64);
                //    config->_oled->_oled->setColor(WHITE);
                //    config->_oled->_oled->fillRect(68, 19, 56, 56);
                //    config->_oled->_oled->setColor(BLACK);
                //    config->_oled->_oled->fillRect(72, 23, 48, 48);
                //    config->_oled->_oled->setColor(WHITE);
                //    config->_oled->_oled->fillRect(76, 27, 40, 40);
                //    config->_oled->_oled->display();

                //    config->_oled->_oled->setColor(BLACK);
                //    config->_oled->_oled->fillRect(64, 15, 64, 64);
                //    config->_oled->_oled->setColor(WHITE);
                //    config->_oled->_oled->drawXbm(68, 18, 24, 24, settings_icon_bits);
                //    config->_oled->_oled->display();

                    config->_oled->show_home_layout();

                } else if (strcmp(config->_oled->_menu->get_selected()->display_name, "TEST2") == 0) {
                    config->_oled->popup_msg("Blah blahdee bla blah foobar quxbaazloremipsumdolorsitamat.", 0);

                } else if (strcmp(config->_oled->_menu->get_selected()->display_name, "Run Latest") == 0) {
                    // run most recent (mod date or just uploaded) gcode file, must be homed
                    if (!config->_axes->_homed && config->_kinematics->canHome(0) &&
                                !(config->getMachineType() == Machine::MachineType::EggBot)) {
                        config->_oled->popup_msg("Machine not homed");
                        log_info("Debug path during unhomed: " << config->_oled->_menu->get_recent_file_path().c_str());
                    } else {
                        // : strip the completion prefix on re-run if present.
                        // Copy the std::string locally — get_recent_file_path() returns by value.
                        std::string source_path = config->_oled->_menu->get_recent_file_path();
                        log_info("Passing path to InputFile from Run Latest: " << source_path);
                        std::string stripped_storage;
                        const char* path_to_open = CompletionMark::resolve_with_strip(
                            source_path.c_str(), stripped_storage);
                        config->_oled->_menu->set_completed_file_from_recent(); // store run file path
                        InputFile *infile = new InputFile("sd", path_to_open, WebUI::AuthenticationLevel::LEVEL_ADMIN, allChannels);
                        allChannels.registration(infile);
                    }

                } else if (strcmp(config->_oled->_menu->get_selected()->display_name, "Run Again") == 0) {
                    // run gcode file that was just run, must be homed
                    if (config->_oled->_menu->get_completed_file_path().empty()) {
                        config->_oled->popup_msg("Previously run file not found");
                        log_info("Run Again file path was empty");
                    } else if (!config->_axes->_homed && config->_kinematics->canHome(0) &&
                                !(config->getMachineType() == Machine::MachineType::EggBot)) {
                        config->_oled->popup_msg("Machine not homed");
                    } else {
                        std::string source_path = config->_oled->_menu->get_completed_file_path();
                        launch_sd_file(source_path.c_str());
                    }

                } else if (strcmp(config->_oled->_menu->get_selected()->display_name,
                                  "Run Next (HOLD to skip)") == 0) {
                    Menu::PostrunState st = config->_oled->_menu->compute_postrun_state();
                    if (!st.show_run_next || st.next_path.empty()) {
                        log_info("Run Next: no next file");
                    } else {
                        // Distinguish short press (launch) from long press (skip).
                        // Block-poll is safe here: the machine is idle on the postrun screen.
                        uint32_t threshold = millis() + config->_control->_long_press_ms;
                        while (config->_control->enter_pressed() && millis() < threshold) {
                            delay_ms(10);
                        }
                        bool is_hold = (millis() >= threshold);

                        if (is_hold) {
                            // HOLD: skip the upcoming file by marking it completed without
                            // plotting it. Show a brief toast naming the skipped file, then
                            // let the cooperative popup auto-clear restore the postrun screen.
                            std::string skip_path = st.next_path;
                            std::string skip_name = st.next_name;
                            CompletionMark::mark_completed(skip_path.c_str());
                            std::string toast = "Skipping (marking \xE2\x9C\x93):\n" + skip_name;
                            // Full-screen wipe (preserve_header=false) so the postrun top
                            // line ("Run Next (HOLD to skip)") doesn't linger above the
                            // centered toast. 3s so the skipped filename is readable.
                            config->_oled->popup_msg(toast, 3000, /*preserve_header=*/false);
                            // If skipping exhausted all remaining next files, drop the
                            // "Run Next" entry so the screen returns to the two-icon layout.
                            if (!config->_oled->_menu->compute_postrun_state().show_run_next) {
                                config->_oled->_menu->rebuild_postrun_menu();
                            }
                        } else {
                            // Short press: mark the just-run file done, then launch next.
                            if (!config->_axes->_homed && config->_kinematics->canHome(0) &&
                                    !(config->getMachineType() == Machine::MachineType::EggBot)) {
                                config->_oled->popup_msg("Machine not homed");
                            } else {
                                std::string j = config->_oled->_menu->get_completed_file_path();
                                if (!j.empty()) {
                                    CompletionMark::mark_completed(j.c_str());  // idempotent
                                }
                                std::string next_path = st.next_path;  // copy before launch
                                launch_sd_file(next_path.c_str());
                            }
                        }
                    }

                } else if (strcmp(config->_oled->_menu->get_selected()->display_name, "Z Calibration Position") == 0) {
                    // RC servo Z calibration for EggBot and WaterColorBot
                    // Note: This executes in State::Idle, so motion is already complete
                    if (rcServoZCal) {
                        // Exiting calibration - restore original position using machine coordinates
                        if (rcServoZOriginalPos > -99000.0f) {
                            char cmd[64];
                            snprintf(cmd, sizeof(cmd), "G53 G1 Z%.3f F1000", rcServoZOriginalPos);
                            gc_execute_line(cmd);
                            log_info("RC servo calibration exited, returning to Z" << rcServoZOriginalPos);
                        }
                        // Reset calibration state
                        clearRcServoCalibration();
                    } else {
                        // Entering calibration - save current Z position (machine coordinates)
                        // System is in State::Idle so position is stable
                        float* mpos = get_mpos();
                        rcServoZOriginalPos = mpos[Z_AXIS];  // Save machine Z position (replaces NAN)
                        log_info("RC servo calibration entered, saved position Z" << rcServoZOriginalPos);

                        // Move to calibration position based on machine type
                        if (config->getMachineType() == Machine::MachineType::EggBot) {
                            gc_execute_line((char*)"G1 Z8.75 F1000");
                        } else if (config->getMachineType() == Machine::MachineType::WaterColorBot) {
                            gc_execute_line((char*)"G1 Z6 F1000");
                        }
                        rcServoZCal = true;
                    }

                } else if (strcmp(config->_oled->_menu->get_selected()->display_name, "Draw Bounds") == 0) {
                    config->_oled->popup_msg("Feature in development");

                } else if (strcmp(config->_oled->_menu->get_selected()->display_name, "Update Firmware") == 0){ // Enter FW Menu from Settings menu
                    // Flashing::update_firmware_from_sdcard();
                    log_info("Is Settings Menu: " << config->_oled->_menu->is_settings_menu());
                    ListNodeType *selected_entry = config->_oled->_menu->get_selected();
                    if (selected_entry->child != NULL) {
                        log_info("Entering Firmware Menu");
                        config->_oled->_menu->enter_submenu();
                    } 
                } else if (strcmp(config->_oled->_menu->get_selected()->display_name, "Update Config File") == 0){ // Enter Config Menu from Settings menu
                    log_info("Is Settings Menu: " << config->_oled->_menu->is_settings_menu());
                    ListNodeType *selected_entry = config->_oled->_menu->get_selected();
                    if (selected_entry->child != NULL) {
                        log_info("Entering Config Menu");
                        config->_oled->_menu->enter_submenu();
                    } 
                } else if (strcmp(config->_oled->_menu->get_selected()->display_name, "Back To Run") == 0) {
                    // back button specifically from post-run menu, go back to run menu
                    config->_oled->_menu->return_to_run_menu();
                // Back button
                } else if (strcmp(config->_oled->_menu->get_selected()->display_name, BACK_LABEL) == 0) {
                    // Postrun menu Back resumes the SD arena browser at the pre-run directory
                    if (config->_oled->_menu->is_postrun_menu()) {
                        log_info("Postrun Back button - resuming arena browse");
                        config->_oled->_menu->resume_sd_browse();
                    } else {
                        config->_oled->_menu->exit_submenu();
                    }
                // Download file command if RSS menu
                } else if (config->_oled->_menu->is_rss_menu()) {
#ifdef ENABLE_WIFI
                    WebUI::rssReader.download_file(config->_oled->_menu->get_selected()->path, config->_oled->_menu->get_selected()->display_name);
#endif
                // Catches remaining selections, including the main-menu "Browse Files" entry.
                } else {
                    config->_oled->_menu->enter_submenu();
                }
            }
            break;

        default: break;
    }
}

static int32_t pauseEndTime = 0;
static bool pauseActive = false;

// Reads the ultrasonic sensor and TODO
void protocol_read_ultrasonic() {

    // Bail if ultrasonic sensor not configured
    if (!config->_ultrasonic) return;

    // Process states if sensor active
    if (config->_ultrasonic->is_active()) {

        switch (sys.state) {

            // Ultrasonic sensor does nothing in these states
            case State::ConfigAlarm:
            case State::CheckMode:
            case State::Jog:
            case State::Homing:
            case State::Sleep:
            case State::SafetyDoor:
            case State::Alarm:
            case State::Idle:
                break;

            // Feedhold during a cycle if we're within the pause distance and start timer
            case State::Cycle:

                if (config->_ultrasonic->within_pause_distance()) {
                    protocol_send_event(&feedHoldEvent, false);
                    pauseActive = true;
                }
                break;

            // Resume from feedhold after a pause
            case State::Hold:

                // Once in HOLD, schedule pause for specified time unless already scheduled
                if (pauseActive && pauseEndTime == 0) {
                    pauseEndTime = usToEndTicks(config->_ultrasonic->get_pause_time_ms() * 1000);
                    // pauseEndTime 0 means that a resume is not scheduled. so if we happen to
                    // land on 0 as an end time, just push it back by one microsecond to get off 0.
                    if (pauseEndTime == 0) {
                        pauseEndTime = 1;
                    }

                // Check to see if we should resume from feedhold
                // If pauseEndTime is 0, no pause is pending.
                } else if (pauseActive && pauseEndTime && (getCpuTicks() - pauseEndTime) > 0) {
                    pauseEndTime = 0;

                    // Still have an object in the way, restart the timer
                    if (config->_ultrasonic->within_pause_distance()) {
                        pauseEndTime = usToEndTicks(config->_ultrasonic->get_pause_time_ms() * 1000);
                        // pauseEndTime 0 means that a resume is not scheduled. so if we happen to
                        // land on 0 as an end time, just push it back by one microsecond to get off 0.
                        if (pauseEndTime == 0) {
                            pauseEndTime = 1;
                        }
                        
                    // Otherwise, resume motion
                    } else {
                        pauseActive = false;
                        protocol_send_event(&cycleStartEvent);
                    }
                }
                break;

            default: break;
        }
    }
}

ArgEvent feedOverrideEvent { protocol_do_feed_override };
ArgEvent rapidOverrideEvent { protocol_do_rapid_override };
ArgEvent spindleOverrideEvent { protocol_do_spindle_override };
ArgEvent accessoryOverrideEvent { protocol_do_accessory_override };
ArgEvent limitEvent { protocol_do_limit };
ArgEvent cardDetectEvent { protocol_do_card_detect};

ArgEvent reportStatusEvent { (void (*)(void*))report_realtime_status };

NoArgEvent safetyDoorEvent { request_safety_door };
ArgEvent feedHoldEvent { protocol_do_feedhold };
NoArgEvent cycleStartEvent { protocol_do_cycle_start };
NoArgEvent cycleStopEvent { protocol_do_cycle_stop };
NoArgEvent motionCancelEvent { protocol_do_motion_cancel };
NoArgEvent sleepEvent { protocol_do_sleep };
NoArgEvent debugEvent { report_realtime_debug };
NoArgEvent enterEvent { protocol_do_enter };

// Only mc_reset() is permitted to set rtReset.
NoArgEvent resetEvent { mc_reset };

// The problem is that report_realtime_status needs a channel argument
// Event statusReportEvent { protocol_do_status_report(XXX) };

xQueueHandle event_queue;

void protocol_init() {
    event_queue   = xQueueCreate(10, sizeof(EventItem));
    message_queue = xQueueCreate(10, sizeof(LogMessage));
}

void IRAM_ATTR protocol_send_event_from_ISR(Event* evt, void* arg) {
    EventItem item { evt, arg };
    xQueueSendFromISR(event_queue, &item, NULL);
}
void protocol_send_event(Event* evt, void* arg) {
    EventItem item { evt, arg };
    xQueueSend(event_queue, &item, 0);
}
void protocol_handle_events() {
    EventItem item;
    while (xQueueReceive(event_queue, &item, 0)) {
        //log_debug("event");
        item.event->run(item.arg);
    }
}
