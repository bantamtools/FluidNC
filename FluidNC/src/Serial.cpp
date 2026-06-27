// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
  Serial.cpp - Header for system level commands and real-time processes

  Original Grbl only supports communication via serial port. That is why this
  file is call serial.cpp. FluidNC supports many "channels".

  Channels are sources of commands like the serial port or a bluetooth connection.
  Multiple channels can be active at a time. If a channel asks for status, only the channel will
  receive the reply to the command.

  The serial port acts as the debugging port because it is always on and does not
  need to be reconnected after reboot. Messages about the configuration and other events
  are sent to the serial port automatically, without a request command. These are in the
  [MSG: xxxxxx] format which is part of the Grbl protocol.

  Important: It is up to the user that the channels play well together. Ideally, if one channel
  is sending the gcode, the others should just be doing status, feedhold, etc.

  Channels send line-oriented command (GCode, $$, [ESP...], etc) and realtime commands (?,!.~, etc)
  A line-oriented command is a string of printable characters followed by a '\r' or '\n'
  A realtime commands is a single character with no '\r' or '\n'

  After sending a line-oriented command, a sender must wait for an OK to send another.
  This is because only a certain number of commands can be buffered at a time.
  The system will tell you when it is ready for another one with the OK.

  Realtime commands are recognized at command boundaries -- that is, when the
  channel is not currently accumulating a line.  At a boundary, an incoming
  byte matching is_realtime_command() is dispatched immediately; otherwise it
  becomes line content and is delivered to the parser along with the rest of
  the line.  Ctrl-X (Reset / E-stop) is the single exception: it dispatches
  regardless of line state so emergency stop remains instant.

  This rule lets line-based commands carry bytes in the realtime range as
  content (for example UTF-8 sequences in filenames, or ? / ! / ~ inside a
  gcode comment) without those bytes being stolen by the realtime gate.

  To keep realtime commands responsive, we read all channels as fast as
  possible.  When a byte is claimed as realtime it is acted upon; otherwise
  it is placed into a per-channel line buffer.  When a complete line is
  received, pollChannel returns the associated channel spec.
*/

#include "Serial.h"
#include "UartChannel.h"
#include "Machine/MachineConfig.h"
#include "WebUI/InputBuffer.h"
#include "WebUI/Commands.h"
#include "WebUI/TxRing.h"  // ( Stage 2b) WebUI::TX_DRAIN_BUF for the shared drain buffer
#include "WebUI/WifiConfig.h"
#include "WebUI/WifiServices.h"
#include "MotionControl.h"
#include "Report.h"
#include "System.h"
#include "Protocol.h"  // *Event
#include "InputFile.h"
#include "WebUI/InputBuffer.h"  // XXX could this be a StringStream ?
#include "Main.h"               // display()
#include "StartupLog.h"         // startupLog

#include "Driver/fluidnc_gpio.h"

#include <atomic>
#include <cstring>
#include <vector>
#include <algorithm>
#include <freertos/task.h>  // portMUX_TYPE, TaskHandle_T

std::mutex AllChannels::_mutex;

static TaskHandle_t channelCheckTaskHandle = 0;

#ifdef DEBUG_MEM_USAGE
void heapCheckTask(void* pvParameters) {
    static uint32_t heapSize = 0;
    while (true) {
        std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);  // read fence for settings and whatnot
        uint32_t newHeapSize = xPortGetFreeHeapSize();
        if (newHeapSize != heapSize) {
            heapSize = newHeapSize;
            log_info("heap " << heapSize);
        }
        vTaskDelay(30000 / portTICK_RATE_MS);  // Yield to other tasks

        static UBaseType_t uxHighWaterMark = 0;
#ifdef DEBUG_TASK_STACK
        reportTaskStackSize(uxHighWaterMark);
#endif
    }
}
#endif

// Act upon a realtime character
void execute_realtime_command(Cmd command, Channel& channel) {
    switch (command) {
        case Cmd::Reset:
            log_debug("Cmd::Reset");
            protocol_send_event(&resetEvent);
            break;
        case Cmd::StatusReport:
            report_realtime_status(channel);  // direct call instead of setting flag
            // protocol_send_event(&reportStatusEvent, int(&channel));
            break;
        case Cmd::CycleStart:
            protocol_send_event(&cycleStartEvent);
            if (config->_control->enter_locked()) {
                config->_control->unlock_enter();
            }
            break;
        case Cmd::FeedHold:
            config->_control->lock_enter();
            protocol_send_event(&feedHoldEvent, true);  // Sync before before feedholding
            break;
        case Cmd::SafetyDoor:
            protocol_send_event(&safetyDoorEvent);
            break;
        case Cmd::JogCancel:
            if (sys.state == State::Jog) {  // Block all other states from invoking motion cancel.
                protocol_send_event(&motionCancelEvent);
            }
            break; 
        case Cmd::DebugReport:
            protocol_send_event(&debugEvent);
            break;
        case Cmd::SpindleOvrStop:
            protocol_send_event(&accessoryOverrideEvent, AccessoryOverride::SpindleStopOvr);
            break;
        case Cmd::FeedOvrReset:
            protocol_send_event(&feedOverrideEvent, FeedOverride::Default);
            break;
        case Cmd::FeedOvrCoarsePlus:
            protocol_send_event(&feedOverrideEvent, FeedOverride::CoarseIncrement);
            break;
        case Cmd::FeedOvrCoarseMinus:
            protocol_send_event(&feedOverrideEvent, -FeedOverride::CoarseIncrement);
            break;
        case Cmd::FeedOvrFinePlus:
            protocol_send_event(&feedOverrideEvent, FeedOverride::FineIncrement);
            break;
        case Cmd::FeedOvrFineMinus:
            protocol_send_event(&feedOverrideEvent, -FeedOverride::FineIncrement);
            break;
        case Cmd::RapidOvrReset:
            protocol_send_event(&rapidOverrideEvent, RapidOverride::Default);
            break;
        case Cmd::RapidOvrMedium:
            protocol_send_event(&rapidOverrideEvent, RapidOverride::Medium);
            break;
        case Cmd::RapidOvrLow:
            protocol_send_event(&rapidOverrideEvent, RapidOverride::Low);
            break;
        case Cmd::RapidOvrExtraLow:
            protocol_send_event(&rapidOverrideEvent, RapidOverride::ExtraLow);
            break;
        case Cmd::SpindleOvrReset:
            protocol_send_event(&spindleOverrideEvent, SpindleSpeedOverride::Default);
            break;
        case Cmd::SpindleOvrCoarsePlus:
            protocol_send_event(&spindleOverrideEvent, SpindleSpeedOverride::CoarseIncrement);
            break;
        case Cmd::SpindleOvrCoarseMinus:
            protocol_send_event(&spindleOverrideEvent, -SpindleSpeedOverride::CoarseIncrement);
            break;
        case Cmd::SpindleOvrFinePlus:
            protocol_send_event(&spindleOverrideEvent, SpindleSpeedOverride::FineIncrement);
            break;
        case Cmd::SpindleOvrFineMinus:
            protocol_send_event(&spindleOverrideEvent, -SpindleSpeedOverride::FineIncrement);
            break;
        case Cmd::CoolantFloodOvrToggle:
            protocol_send_event(&accessoryOverrideEvent, AccessoryOverride::FloodToggle);
            break;
        case Cmd::CoolantMistOvrToggle:
            protocol_send_event(&accessoryOverrideEvent, AccessoryOverride::MistToggle);
            break;
        case Cmd::Macro0:
            protocol_send_event(&macro0Event);
            break;
        case Cmd::Macro1:
            protocol_send_event(&macro1Event);
            break;
        case Cmd::Macro2:
            protocol_send_event(&macro2Event);
            break;
        case Cmd::Macro3:
            protocol_send_event(&macro3Event);
            break;
        default:
            // Diagnostic hook: any byte that passed is_realtime_command() but
            // has no Cmd enum handler used to vanish silently here.  Log it
            // via log_debug_to (per-channel, bypasses AllChannels::_mutex
            // which pollLine holds -- broadcast log_debug would deadlock).
            // Gated by $Message/Level=Debug; off by default.
            log_debug_to(channel, "unhandled realtime Cmd " << to_hex(uint8_t(command)));
            break;
    }
}

// checks to see if a character is a realtime character
bool is_realtime_command(uint8_t data) {
    if (data >= 0x80) {
        return true;
    }
    auto cmd = static_cast<Cmd>(data);
    return cmd == Cmd::Reset || cmd == Cmd::StatusReport || cmd == Cmd::CycleStart || cmd == Cmd::FeedHold;
}

void AllChannels::init() {
    registration(&WebUI::inputBuffer);  // Macros
    registration(&startupLog);          // Early startup messages for $SS
#ifdef DEBUG_MEM_USAGE 
    // Check memory usage
    xTaskCreate(heapCheckTask, "heapCheckTask", 4096, NULL, 3, NULL);
#endif
}

//  Option C: number of broadcast snapshot iterations currently in flight.
// While nonzero, the kill-drain in pollLine() defers freeing a channel, because
// a snapshot taken before the channel was deregistered may still dereference it.
static std::atomic<int> _broadcastDepth { 0 };

// ( Phase-2 Stage 1) See Serial.h. A successful network TX-ring push sets
// g_net_tx_dirty; output_loop exchanges it to false before each drain pass.
std::atomic<bool>     g_net_tx_dirty { false };
std::atomic<uint32_t> g_net_tx_dropped { 0 };

// ( SF-3) The core-1 main loop's currently-dispatching channel (defined in
// Protocol.cpp). The kill-drain below must NOT free it while core-1 still holds
// and dereferences it, or core-1 crashes (LoadProhibited at ->ack()).
extern std::atomic<Channel*> activeChannel;

std::vector<Channel*> AllChannels::snapshotChannels() {
    _mutex.lock();
    std::vector<Channel*> snapshot(_channelq);
    _broadcastDepth.fetch_add(1, std::memory_order_acq_rel);
    _mutex.unlock();
    return snapshot;
}
void AllChannels::releaseSnapshot() {
    _broadcastDepth.fetch_sub(1, std::memory_order_acq_rel);
}

void AllChannels::waitForDrainQuiescent() {
    // ( Stage 1) Wait out any in-flight broadcast/TX-drain snapshot before a
    // caller frees a resource those snapshots dereference (the WebSocketsServer
    // behind WSChannel::_server). _broadcastDepth defers freeing the CHANNEL, but
    // not the server, so a concurrent delete races a mid-send drainTx(). Callers
    // markDead() every affected channel first; once depth hits 0 here, no in-flight
    // deref remains and any new drain bails on its _dead check. Teardown path only
    // (not a hot path); bounded by the drain's per-pass socket waits, so it yields.
    while (_broadcastDepth.load(std::memory_order_acquire) != 0) {
        vTaskDelay(1);
    }
}

void AllChannels::kill(Channel* channel) {
    // Idempotent: if the channel has already been queued once, do not
    // queue it again.  Preventing duplicate entries is what keeps the
    // drain at the top of pollLine() from running `delete` twice on the
    // same pointer, which would corrupt the heap on the second pass.
    if (!channel || !channel->setKilled()) {
        return;
    }
    xQueueSend(_killQueue, &channel, 0);
}

void AllChannels::registration(Channel* channel) {
    _mutex.lock();
    _channelq.push_back(channel);
    _mutex.unlock();
}
void AllChannels::deregistration(Channel* channel) {
    _mutex.lock();
    if (channel == _lastChannel) {
        _lastChannel = nullptr;
    }
    _channelq.erase(std::remove(_channelq.begin(), _channelq.end(), channel), _channelq.end());
    _mutex.unlock();
}

void AllChannels::listChannels(Channel& out) {
    _mutex.lock();
    std::string retval;
    for (auto channel : _channelq) {
        if (channel) {
            log_to(out, channel->name());
        }
    }
    _mutex.unlock();
}

// All of the fan-out methods below iterate a SNAPSHOT of the channel list with
// _mutex released ( Option C). The previous code held _mutex across every
// per-channel call, so a single channel whose write/notify blocked -- a
// saturated socket, or a write that re-enters AllChannels (the OLED status
// parser registering a file after auto-home) -- stalled the polling task and,
// on the same task, deadlocked on the non-recursive _mutex. With the lock held
// only for the O(n) pointer copy, a slow channel can no longer freeze the rest.
void AllChannels::flushRx() {
    auto snapshot = snapshotChannels();
    for (auto channel : snapshot) {
        if (channel) {
            channel->flushRx();
        }
    }
    releaseSnapshot();
}

size_t AllChannels::write(uint8_t data) {
    auto snapshot = snapshotChannels();
    for (auto channel : snapshot) {
        if (channel) {
            channel->write(data);
        }
    }
    releaseSnapshot();
    return 1;
}
void AllChannels::notifyWco(void) {
    auto snapshot = snapshotChannels();
    for (auto channel : snapshot) {
        if (channel) {
            channel->notifyWco();
        }
    }
    releaseSnapshot();
}
void AllChannels::notifyNgc(CoordIndex coord) {
    auto snapshot = snapshotChannels();
    for (auto channel : snapshot) {
        if (channel) {
            channel->notifyNgc(coord);
        }
    }
    releaseSnapshot();
}

void AllChannels::stopJob() {
    auto snapshot = snapshotChannels();
    for (auto channel : snapshot) {
        if (channel) {
            channel->stopJob();
        }
    }
    releaseSnapshot();
}

size_t AllChannels::write(const uint8_t* buffer, size_t length) {
    auto snapshot = snapshotChannels();
    for (auto channel : snapshot) {
        if (channel) {
            //  Per-LEAF broadcast timing. deliver_message can only see the
            // aggregate "all" name; this names the specific leaf (uart/oled/...)
            // whose write parks the drainer. ( Phase-2 Stage 1) The network
            // leaves now just append to their TX ring here (microseconds); the
            // actual blocking socket send + its g_out_netwrite [HB] attribution
            // moved to drainTx(). USB/OLED still write synchronously, so this
            // timing still localizes a slow non-network leaf.
            uint32_t t0 = millis();
            channel->write(buffer, length);
            uint32_t dt = millis() - t0;
            if (dt >= 20) {
                if (dt > g_bcast_slow_ms) {
                    g_bcast_slow_ms = dt;
                }
                strncpy(g_bcast_slow_leaf, channel->name(), sizeof(g_bcast_slow_leaf) - 1);
                g_bcast_slow_leaf[sizeof(g_bcast_slow_leaf) - 1] = '\0';
                ++g_bcast_slow_count;
            }
        }
    }
    releaseSnapshot();
    return length;
}

// ( Phase-2 Stage 1) Drain every network channel's TX ring -> socket send.
// Called from output_loop (the wifi_task after Stage 2), gated by g_net_tx_dirty.
// Enumerate via the broadcast snapshot so _broadcastDepth>0 defers freeing any
// channel for the drain's duration (B3 lifetime: the kill-drain in pollLine()
// will not delete a channel still referenced by an in-flight snapshot). The
// single shared scratch buffer is safe because the consumer is one task.
bool AllChannels::drainTxRings() {
    // ( Stage 2b, review D2) Size from the shared TxRing constant; each TxRing<CAP>
    // static_asserts CAP <= TX_DRAIN_BUF, so peek() can always fit any stored record.
    static uint8_t s_drainBuf[WebUI::TX_DRAIN_BUF];  // static .bss, not heap
    auto           snapshot     = snapshotChannels();
    bool           moreLeft     = false;
    bool           committedAny = false;
    for (auto channel : snapshot) {
        if (channel && channel->drainTx(s_drainBuf, sizeof s_drainBuf, committedAny)) {
            moreLeft = true;
        }
    }
    releaseSnapshot();
    // Re-arm only when we made progress AND records remain: a healthy bound-hit
    // keeps draining, but a purely back-pressured socket waits for the next push
    // rather than busy-spinning the output loop.
    return moreLeft && committedAny;
}
Channel* AllChannels::pollLine(char* line) {
    Channel*              deadChannel;
    std::vector<Channel*> deferred;
    while (xQueueReceive(_killQueue, &deadChannel, 0)) {
        // Deregister first so no NEW broadcast snapshot can include it. Then
        // free it only once no holder still references the raw pointer:
        //   - no broadcast snapshot is in flight (_broadcastDepth), and
        //   - no queued output message still points at it (pendingOut).
        // A snapshot or a queued message captured before this deregistration
        // may still dereference the channel, so otherwise defer to a later
        // poll. Collect deferrals and re-queue after the loop so we do not
        // re-dequeue them immediately.
        deregistration(deadChannel);
        if (_broadcastDepth.load(std::memory_order_acquire) == 0 && deadChannel->pendingOut() == 0 &&
            deadChannel != activeChannel.load()) {  // ( SF-3) not the channel core-1 is dispatching
            delete deadChannel;
        } else {
            deferred.push_back(deadChannel);
        }
    }
    for (auto ch : deferred) {
        xQueueSend(_killQueue, &ch, 0);
    }

    // To avoid starving other channels when one has a lot
    // of traffic, we poll the other channels before the last
    // one that returned a line.
    g_poll_phase = 40;  // ( r3) blocked here == waiting on _mutex
    _mutex.lock();

    for (auto channel : _channelq) {
        // Skip the last channel in the loop
        if (channel != _lastChannel && channel) {
            g_poll_phase = 41;  // ( r3) inside a channel's pollLine (read/service path)
#ifdef DEBUG_HEAP_INSTRUMENTATION
            // ( review A3) g_poll_chan only feeds the debug [HB] pchan= field. Gate
            // the strncpy out of the prod build (it ran per-channel/iteration on the
            // poller hot path AND inside _mutex, shared with registration()/kill()).
            strncpy(g_poll_chan, channel->name(), sizeof(g_poll_chan) - 1);
            g_poll_chan[sizeof(g_poll_chan) - 1] = '\0';
#endif
            if (channel->pollLine(line)) {
                _lastChannel = channel;
                _mutex.unlock();
                g_poll_phase = 49;
                return _lastChannel;
            }
        }
    }

    // Make a local copy of _lastChannel while still holding the mutex
    // to avoid race condition with deregistration
    Channel* lastChannel = _lastChannel;
    _mutex.unlock();

    // If no other channel returned a line, try the last one
    if (lastChannel) {
        g_poll_phase = 42;  // ( r3) inside the last channel's pollLine
#ifdef DEBUG_HEAP_INSTRUMENTATION
        strncpy(g_poll_chan, lastChannel->name(), sizeof(g_poll_chan) - 1);
        g_poll_chan[sizeof(g_poll_chan) - 1] = '\0';
#endif
        if (lastChannel->pollLine(line)) {
            g_poll_phase = 49;
            return lastChannel;
        }
    }
    g_poll_phase = 49;  // ( r3) pollLine done, nothing ready
    _lastChannel = nullptr;
    return nullptr;
}

AllChannels allChannels;

Channel* pollChannels(char* line) {
    poll_gpios();
    // Throttle polling when we are not ready for a line, thus preventing
    // planner buffer starvation due to not calling Stepper::prep_buffer()
    // frequently enough, which is normally called periodically at the end
    // of protocol_exec_rt_system() via protocol_execute_realtime().
    static int counter = 0;
    if (line) {
        counter = 0;
    }
    if (counter > 0) {
        --counter;
        return nullptr;
    }
    counter = 50;

    Channel* retval = allChannels.pollLine(line);

    WebUI::COMMANDS::handle();      // Handles ESP restart (non-blocking latch)
    // ( Stage 2) wifi_config.handle() (WS/HTTP/telnet/OTA/rss + reconnect) MOVED
    // off the poller onto the dedicated wifi_task — its blocking socket ops can no
    // longer freeze input/OLED. The poller now does ZERO blocking socket I/O.

    return retval;
}
