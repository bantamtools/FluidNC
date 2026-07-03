// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "InputFile.h"
#include "LineAssembly.h"

#include "CompletionMark.h"
#include "FileEndOutcome.h"
#include "Report.h"
#include "Protocol.h"
#include "Machine/MachineConfig.h"  // config
#include "GCode.h"  // For gc_clear_m0_comment()

#include <string>    // std::string
#include <utility>   // std::move, used in the Error::Eof block below

InputFile::InputFile(const char* defaultFs, const char* path, WebUI::AuthenticationLevel auth_level, Channel& out) :
    FileStream(path, "r", defaultFs), _auth_level(auth_level), _out(out), _line_num(0)  {
    log_info("Run file opened");  // Used by OLED for elapsed time
    gc_saw_program_end = false; // clear flag for truncated file checking

    // Clear RC servo calibration state when starting a file
    clearRcServoCalibration();

    // Clear comments when opening new file
    if (config && config->_oled) {
        // Latch the plot/progress-screen gate deterministically here, at the single common open
        // point for every launch path (auto-home post-homing open, OLED-menu launch_sd_file,
        // $-command/WebUI runFile). _file_job_running was previously set ONLY by the OLED parsing
        // the DROPPABLE "Run file opened" broadcast above; if that line was dropped at the bounded
        // message queue, the OLED stayed on the prior screen for the whole job. Setting it directly
        // removes that dependence on the queue.
        config->_oled->set_file_job_running(true);
        config->_oled->set_comment("", false);  // Clear immediate
        config->_oled->clear_m0_comment();      // Clear M0
    }
    gc_clear_m0_comment();  // Clear pending M0 comment
}
/*
  Read a line from the file
  Returns Error::Ok if a line was read, even if the line was empty.
  Returns Error::EOF on end of file.
  Returns other Error code on error, after displaying a message.
*/
int InputFile::readByteThunk(void* ctx) {
    return static_cast<InputFile*>(ctx)->read();
}

Error InputFile::readLine(char* line, int maxlen) {
    ++_line_num;
    int len = 0;
    return term_to_error(assemble_line(line, maxlen, len, readByteThunk, this), _ended_midline);
}

// return a percentage complete 50.5 = 50.5%
float InputFile::percent_complete() {
    return (float)position() / (float)size() * 100.0f;
}

#include <sstream>
#include <iomanip>
#include "Machine/MachineConfig.h"

void InputFile::ack(Error status) {
    if (status != Error::Ok) {
        log_error(static_cast<int>(status) << " (" << errorString(status) << ") in " << path()
                  << " at line " << getLineNumber());
        if (status != Error::GcodeUnsupportedCommand) {
            // Do not stop on unsupported commands because most senders do not
            // Stop the file job on other errors
            _hadError = true;
            _notifyf("File job error", "Error:%d in %s at line: %d", status, path().c_str(),
                     getLineNumber());
            config->_oled->_menu->set_last_file_succeeded(false);
            config->_oled->_menu->set_last_file_error(false);
            allChannels.kill(this);
            return;
        }
    }
    _readyNext = true;
}

std::string InputFile::_progress = "";

Channel* InputFile::pollLine(char* line) {
    // File input never returns realtime characters, so we do nothing
    // if line is null.
    if (!_readyNext || !line) {
        return nullptr;
    }
    switch (auto err = readLine(line, Channel::maxLine)) {
        case Error::Ok: {
            std::ostringstream s;
            s << "SD:" << std::fixed << std::setprecision(2) << percent_complete() << "," << path().c_str();
            _progress = s.str();
        }
            return &allChannels;
        case Error::Eof: {
            FileEndOutcome outcome = evaluate_file_end(gc_saw_program_end, _ended_midline);

            _hadError = outcome.had_error;
            _progress = "";

            if (outcome.had_error) {
                // Truncated: ended mid-line with no M2/M30. The partial final line
                // was routed here and not executed. Non-fatal, but a trailing
                // command may have been dropped, so it is not marked done.
                log_warn("File ended without end-of-program marker; last line incomplete in "
                         << path());
                _notifyf("File job error", "Unexpected file end in %s", path().c_str());
            } else {
                _notifyf("File job done", "%s file job succeeded", path().c_str());
                log_msg(path() << " file job succeeded");
                uint32_t heap_free = ESP.getFreeHeap();
                float    heap_kb   = heap_free / 1024.0;
#ifdef DEBUG_STACK_USAGE
                const uint32_t STACK_TOTAL_WORDS = 6144; // ARDUINO_LOOP_STACK_SIZE in main.cpp
                uint32_t stack_words     = uxTaskGetStackHighWaterMark(NULL);
                float    stack_kb        = (stack_words * 4) / 1024.0;
                float    stack_total_kb  = (STACK_TOTAL_WORDS * 4) / 1024.0;
                uint32_t stack_used_pct  = ((STACK_TOTAL_WORDS - stack_words) * 100) / STACK_TOTAL_WORDS;
                log_info("File completed - Stack: " << stack_kb << " kB free / "
                         << stack_total_kb << " kB total (" << stack_used_pct
                         << "% peak used) | Heap: " << heap_kb << " kB free");
#else
                log_info("File completed - Heap: " << heap_kb << " kB free");
#endif
            }

            // Completion-mark only the success paths. final_path tracks the
            // on-disk name so set_completed_file reflects any rename the mark
            // performs; consumers ("Run Again", end-of-plot display) open it.
            std::string final_path = path();
            if (outcome.mark_completed && CompletionMark::completion_marking_enabled()) {
                Error me = CompletionMark::mark_completed(final_path.c_str());
                if (me == Error::Ok) {
                    std::string marked = CompletionMark::compute_marked_path(final_path.c_str());
                    if (!marked.empty()) {
                        final_path = std::move(marked);
                    }
                } else {
                    // Rename failed; disk still unmarked; final_path stays the
                    // unmarked name so stored matches disk.
                    log_warn("CompletionMark: failed to mark " << final_path
                             << " (Error " << static_cast<int>(me) << ")");
                }
            }

            if (config->_oled) {
                config->_oled->_menu->set_completed_file(final_path.c_str());
                config->_oled->_menu->set_last_file_succeeded(outcome.job_succeeded);
                config->_oled->_menu->set_last_file_error(outcome.had_error);
                if (outcome.message) {
                    // Persistent: clears only on a user button click. A mid-job
                    // ending must be acknowledged; a brief auto-clearing toast
                    // would be missed.
                    config->_oled->popup_msg(outcome.message, 0);
                }
            }

            allChannels.kill(this);
            return nullptr;
        }
        default: {
            _hadError = true;
            _progress = "";
            log_error(static_cast<int>(err) << " (" << errorString(err) << ") in " << path() << " at line " << getLineNumber());
            uint32_t heap_free = ESP.getFreeHeap();
            float heap_kb = heap_free / 1024.0;

#ifdef DEBUG_STACK_USAGE
            const uint32_t STACK_TOTAL_WORDS = 6144; // From ARDUINO_LOOP_STACK_SIZE in main.cpp
            uint32_t stack_words = uxTaskGetStackHighWaterMark(NULL);
            float stack_kb = (stack_words * 4) / 1024.0;
            float stack_total_kb = (STACK_TOTAL_WORDS * 4) / 1024.0;
            uint32_t stack_used_pct = ((STACK_TOTAL_WORDS - stack_words) * 100) / STACK_TOTAL_WORDS;
            log_info("File failed - Stack: " << stack_kb << " kB free / " << stack_total_kb << " kB total (" << stack_used_pct << "% peak used) | Heap: " << heap_kb << " kB free");
#else
            log_info("File failed - Heap: " << heap_kb << " kB free");
#endif
            config->_oled->_menu->set_completed_file(path().c_str());
            config->_oled->_menu->set_last_file_succeeded(false);
            config->_oled->_menu->set_last_file_error(false);
            allChannels.kill(this);
            return nullptr;
        }
    }
}

void InputFile::stopJob() {
    //Report print stopped
    _hadError = true;
    _notifyf("File print canceled", "Reset during file job at line: %d", getLineNumber());
    log_info("Reset during file job at line: " << getLineNumber());
    uint32_t heap_free = ESP.getFreeHeap();
    float heap_kb = heap_free / 1024.0;

#ifdef DEBUG_STACK_USAGE
    const uint32_t STACK_TOTAL_WORDS = 6144; // From ARDUINO_LOOP_STACK_SIZE in main.cpp
    uint32_t stack_words = uxTaskGetStackHighWaterMark(NULL);
    float stack_kb = (stack_words * 4) / 1024.0;
    float stack_total_kb = (STACK_TOTAL_WORDS * 4) / 1024.0;
    uint32_t stack_used_pct = ((STACK_TOTAL_WORDS - stack_words) * 100) / STACK_TOTAL_WORDS;
    log_info("File stopped - Stack: " << stack_kb << " kB free / " << stack_total_kb << " kB total (" << stack_used_pct << "% peak used) | Heap: " << heap_kb << " kB free");
#else
    log_info("File stopped - Heap: " << heap_kb << " kB free");
#endif
    config->_oled->_menu->set_completed_file(path().c_str());
    config->_oled->_menu->set_last_file_succeeded(false);
    config->_oled->_menu->set_last_file_error(false);
    // (Bantam) A cancelled/aborted plot must re-home before the next run; the
    // abrupt stop may have lost position. stopJob() runs only on a reset-driven
    // abort of an active file job (from Cycle or a paused Hold:0), never on
    // normal EOF completion — so success never unhomes.
    if (config && config->_axes) {
        config->_axes->set_unhomed();
    }
    allChannels.kill(this);
}

InputFile::~InputFile() {
    log_info("Run file closed");  // Used by OLED for elapsed time
    
    // Clear comments when closing file
    if (config && config->_oled) {
        config->_oled->set_comment("", false);  // Clear immediate
        config->_oled->clear_m0_comment();      // Clear M0
    }
    gc_clear_m0_comment();  // Clear pending M0 comment
    
    _progress = "";

    if(config->_oled){
        // Wait for queued motion to finish before gc_sync_position() below, but only
        // for a normally-ended program (M2/M30 seen, no error). A file that ends
        // without a program-end marker, or with an error, is torn down without this
        // wait: such a job issues no program-end, so the run state does not clear here
        // and blocking would spin until it cleared on its own. The machine instead
        // settles to Idle through the normal protocol loop after teardown.
        if (!_hadError && gc_saw_program_end) {
            protocol_buffer_synchronize();
        }

        config->_oled->set_file_job_running(false);
        config->_oled->_menu->go_to_postrun_menu();
        // config->_oled->refresh_display();  // Makes sure we clear the elapsed time display
    }

    // Sync position from motor steps, then normalize rotary axes
    gc_sync_position();
    gc_reset_winding_offsets();
}
