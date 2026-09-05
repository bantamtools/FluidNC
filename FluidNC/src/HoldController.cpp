// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license (see LICENSE).
//
// The feed-hold / park / resume state machine, moved out of Protocol.cpp during
// the pause-simulator work so it can also be linked into the host simulator
// (env:pausesim). Every non-portable call the machine makes goes through the
// HoldPorts function table (see HoldController.h); the firmware installs real
// functions below, env:pausesim installs recorders. The bodies are no longer
// verbatim: they carry the fixes for ,  and .
#include "HoldController.h"
#include "Protocol.h"
#include "Parking.h"
#include "Planner.h"
#include "Stepper.h"
#include "System.h"
#include "Report.h"
#include "MotionControl.h"
#include "PauseInstruction.h"
#include "GCode.h"    // emit_pause_instruction_clear, gc_state
#include "Limits.h"   // soft_limit
#include "Logging.h"  // log_info
#include "Machine/MachineConfig.h"
#include "Spindles/Spindle.h"
#ifdef ENABLE_WIFI
#    include <WiFi.h>
#endif

#ifndef PAUSESIM
namespace {
    void fw_report_realtime_status() { report_realtime_status(allChannels); }
    void fw_report_feedback_message(Message m) { report_feedback_message(m); }
    void fw_oled_refresh() { if (config && config->_oled) config->_oled->refresh_display(); }
    void fw_oled_clear_m0_comment() { if (config && config->_oled) config->_oled->clear_m0_comment(); }
    void fw_emit_pause_instruction_clear() { emit_pause_instruction_clear(); }
    void fw_initiate_homing_cycle() { protocol_initiate_homing_cycle(); }
    void fw_manage_spindle() { protocol_manage_spindle(); }
    void fw_wifi_resume() {
#    ifdef ENABLE_WIFI
        WiFi.setAutoReconnect(true);
        if (WiFi.status() != WL_CONNECTED) {
            WiFi.reconnect();
        }
#    endif
    }
    // Designated initialisers, in struct order: a positional list here silently
    // mis-wires the table if a HoldPorts member is ever added or reordered.
    const HoldPorts firmware_ports {
        .report_realtime_status       = fw_report_realtime_status,
        .report_feedback_message      = fw_report_feedback_message,
        .oled_refresh                 = fw_oled_refresh,
        .oled_clear_m0_comment        = fw_oled_clear_m0_comment,
        .emit_pause_instruction_clear = fw_emit_pause_instruction_clear,
        .initiate_homing_cycle        = fw_initiate_homing_cycle,
        .manage_spindle               = fw_manage_spindle,
        .wifi_resume                  = fw_wifi_resume,
    };
}
const HoldPorts& hold_ports() { return firmware_ports; }
#else
namespace {
    void noop() {}
    void noop_msg(Message) {}
    HoldPorts host_ports {
        .report_realtime_status       = noop,
        .report_feedback_message      = noop_msg,
        .oled_refresh                 = noop,
        .oled_clear_m0_comment        = noop,
        .emit_pause_instruction_clear = noop,
        .initiate_homing_cycle        = noop,
        .manage_spindle               = noop,
        .wifi_resume                  = noop,
    };
}
const HoldPorts& hold_ports() { return host_ports; }
void set_hold_ports(const HoldPorts& p) { host_ports = p; }
#endif

SpindleStop spindle_stop_ovr;

void protocol_start_holding() {
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

void protocol_cancel_jogging() {
    if (!sys.suspend.bit.motionCancel) {
        sys.suspend.bit.jogCancel = true;
    }
}

void protocol_hold_complete() {
    sys.suspend.value            = 0;
    sys.suspend.bit.holdComplete = true;
}

void protocol_do_feedhold(void *arg) {

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
#ifdef DEBUG_PARK_DIAG
            log_warn("PARK-DIAG feedhold-edge sync=" << sync << " state=Cycle->Hold");  //  diagnostic (dev builds only)
            config->_parking->logState("feedhold-edge");
#endif
            hold_ports().oled_refresh();  // Immediately update OLED to show "Pausing"
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
    //  Push the Hold edge immediately (Decel/"Hold:1") for prompt pause
    // feedback; the emit in protocol_exec_rt_suspend's retract-complete branch
    // sends "Hold:0" later, at full stop.
    hold_ports().report_realtime_status();
}

void protocol_do_initiate_cycle() {
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
        hold_ports().wifi_resume();
    }
    //  Push the resulting state edge (Run/Jog from wake_up above, or Idle).
    // Best-effort/droppable; does NOT touch g_last_emitted_token (backstop owns it).
    hold_ports().report_realtime_status();
}

//  Consume a cycle start that was deferred during a parking retract. Single shot.
// Called from BOTH places the suspend loop sets suspend.retractComplete: the ordinary
// !retractComplete branch, and the deferred-pause re-park , which calls park(true)
// after clearing retractComplete and so runs a second retract under the same guard. A
// replay attached to only one of them strands the flag until an unrelated later retract.
static void replay_deferred_resume() {
    if (sys.deferredResumeRequest) {
        sys.deferredResumeRequest = false;
        protocol_send_event(&cycleStartEvent);
    }
}

void protocol_do_cycle_start() {
    //  A cycle start serviced inside Parking::moveto during the retract would
    // reach protocol_do_initiate_cycle (retractComplete is set only after park()
    // returns), clear executeSysMotion mid-move and desync the planner. Defer it;
    // the suspend loop replays it once the retract completes -- at either of the two
    // retract-complete sites (see replay_deferred_resume above). Unpark is not
    // affected (retractComplete is already true there). Hold-only: a cycle start
    // during a SafetyDoor retract is a no-op today (neither restoreComplete nor
    // retractComplete is set), and this fix must not turn it into a queued resume.
    if (sys.state == State::Hold && sys.parkingInProgress && !sys.suspend.bit.retractComplete) {
        sys.deferredResumeRequest = true;
        return;
    }
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
            hold_ports().initiate_homing_cycle();
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
                        hold_ports().oled_clear_m0_comment();
                        //  Clear the host pause instruction on resume. This is
                        // a separate port call from oled_clear_m0_comment() above, so
                        // the CLEAR token is not OLED-gated: the config->_oled null
                        // guard now lives inside the fw_oled_clear_m0_comment port
                        // wrapper and does not reach here. Self-gated on a non-empty
                        // cache, so a bare user feed-hold resume (no M0) emits nothing
                        // (§4.3).
                        hold_ports().emit_pause_instruction_clear();
                        sys.suspend.bit.initiateRestore = true;
                        // Force display refresh to show "Resuming..." before unpark blocks
                        hold_ports().oled_refresh();
                    // Otherwise, resume
                    } else {
                        // Clear M0 comment when resuming
                        hold_ports().oled_clear_m0_comment();
                        hold_ports().emit_pause_instruction_clear();  //  see parking-path note above
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

// Handles system suspend procedures, such as feed hold, safety door, and parking motion.
// The system will enter this loop, create local variables for suspend tasks, and return to
// whatever function that invoked the suspend, resuming normal operation.
void protocol_exec_rt_suspend() {
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

                    replay_deferred_resume();  //  a resume that arrived mid-retract

                    // Send status report to update OLED after parking completes
                    hold_ports().report_realtime_status();

                    if (config->_control->enter_locked()) {
                        config->_control->unlock_enter();
                    }
                } else {
                    if (sys.state == State::Sleep) {
                        hold_ports().report_feedback_message(Message::SleepMode);
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
                                
                                //  Mark the retract done. Without this the next loop pass
                                // takes the !retractComplete branch, calls park(false), and
                                // overwrites restore_target with the parked Z. That pass was also
                                // the one reporting the new Hold:0 and releasing the enter-button
                                // lockout a serial `!` takes (protocol_do_feedhold returns early
                                // while parking, so it never unlocks), so both are mirrored here.
                                sys.suspend.bit.retractComplete = true;
                                hold_ports().report_realtime_status();
                                if (config->_control->enter_locked()) {
                                    config->_control->unlock_enter();
                                }

                                //  This re-park ran a second retract under the deferral
                                // guard (parkingInProgress set, retractComplete clear), so a `~`
                                // during it is waiting here, not at the branch above.
                                replay_deferred_resume();

                                // Stay in Hold:0 - no state change, no resume (unless a resume
                                // was deferred during the re-park and just got replayed)
                            } else {
                                // Only resume if no deferred pause
                                sys.state = State::Idle;
                                protocol_send_event(&cycleStartEvent);  // Resume program
                            }
                        }
                    }
                }
            } else {
                hold_ports().manage_spindle();
                if (!config->_parking->park_on_feedhold() && config->_control->enter_locked()) {
                    config->_control->unlock_enter();   // Unlock enter button once hold complete
                }
            }
        }
        protocol_exec_rt_system();
    }
}

// Was the body of the Hold/SafetyDoor/Sleep case in protocol_do_cycle_stop()
// in Protocol.cpp. Returns true when the case handled the stop and
// the dispatcher must `break`; false means fall through to the Idle branch
// exactly as the original fall-through did.
bool hold_cycle_stop() {
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
            hold_ports().report_realtime_status();
        } else {
            // This is likely parking motion completing - send status report to update OLED
            if (sys.state == State::Hold && sys.suspend.bit.holdComplete) {
                hold_ports().report_realtime_status();
            }
        }
        sys.step_control.executeHold      = false;
        sys.step_control.executeSysMotion = false;
        return true;
    }
    return false;
}
