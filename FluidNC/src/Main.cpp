// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#ifndef UNIT_TEST

#    include "Main.h"
#    include "Machine/MachineConfig.h"
#    include "Machine/BoardDetection.h"

#    include "Config.h"
#    include "Report.h"
#    include "Settings.h"
#    include "SettingsDefinitions.h"
#    include "Limits.h"
#    include "Protocol.h"
#    include "System.h"
#    include "UartChannel.h"
#    include "UsbChannel.h"
#    include "MotionControl.h"
#    include "Platform.h"
#    include "StartupLog.h"

#    include "WebUI/TelnetServer.h"
#    include "WebUI/InputBuffer.h"

#    include "WebUI/WifiConfig.h"
#    include "Driver/localfs.h"

#    include "Encoder.h"
#    include "Ultrasonic.h"
#    include "OLED.h"
#    include "WifiSetupFile.h"
#    include <cstring>   // strcmp, strlen
#    include <cctype>    // isalpha, isdigit, tolower
#    include <freertos/task.h>  // uxTaskGetStackHighWaterMark

extern void make_user_commands();

static const uint32_t LOOP_STACK_WORDS = 6144;  // ARDUINO_LOOP_STACK_SIZE

static void log_startup_resources(const char* label) {
    uint32_t heap_free   = xPortGetFreeHeapSize();
    uint32_t stack_words = uxTaskGetStackHighWaterMark(NULL);
    float heap_kb        = heap_free / 1024.0f;
    float stack_kb       = (stack_words * 4) / 1024.0f;
    float stack_total_kb = (LOOP_STACK_WORDS * 4) / 1024.0f;
    uint32_t stack_pct   = ((LOOP_STACK_WORDS - stack_words) * 100) / LOOP_STACK_WORDS;
    log_info(label << " - Stack: " << stack_kb << " kB free / "
             << stack_total_kb << " kB total (" << stack_pct
             << "% used) | Heap: " << heap_kb << " kB free");
}

// Derive a valid hostname from the config machine name.
// Lowercase, replace spaces with hyphens, drop invalid chars.
// Only runs when config name changes (tracked via WiFi/ConfigName NVS).
static void derive_hostname_from_config() {
    if (!config || config->_name.empty()) return;

    const char* stored_config_name = WebUI::wifi_config_name->get();
    if (strcmp(stored_config_name, config->_name.c_str()) == 0) {
        return;  // config name unchanged, keep current hostname
    }

    // Config name changed — derive new hostname
    char hostname[33] = {0};
    int j = 0;
    for (int i = 0; i < (int)config->_name.length() && j < 32; i++) {
        char c = config->_name[i];
        if (c == ' ') {
            hostname[j++] = '-';
        } else if (isalpha(c)) {
            hostname[j++] = tolower(c);
        } else if (isdigit(c) || c == '-') {
            hostname[j++] = c;
        }
    }
    hostname[j] = '\0';

    if (strlen(hostname) > 0) {
        WebUI::wifi_hostname->setStringValue(hostname);
        log_info("Hostname derived from config: " << hostname);
    }

    // Update stored config name
    WebUI::wifi_config_name->setStringValue((char*)config->_name.c_str());
}

#ifdef DEBUG_MEMORY
// DEBUG: Memory management task
void mem_task( void * pvParameters ) {

    while(1) {
        log_warn("Free Heap Size -> " << xPortGetFreeHeapSize());
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
#endif

void setup() {
    disableCore0WDT();
    vTaskDelay(pdMS_TO_TICKS(1000));
    try {
    	timing_init();
#ifdef ARDUINO_USB_CDC_ON_BOOT
        usbInit();       // Setup serial port
        Usb0.println();  // create some white space after ESP32 boot info
#else
        uartInit();       // Setup serial port
        Uart0.println();  // create some white space after ESP32 boot info
#endif 
        
        // Mount filesystem BEFORE board detection so it's available for early boot
        if (localfs_mount()) {
            log_error("Cannot mount a local filesystem");
        } else {
            log_info("Local filesystem type is " << localfsName);
        }
        
        // NEW: Detect board type before any configuration or hardware initialization
        // This must happen early to solve the I2C chicken-and-egg problem
        Machine::BoardType boardType = Machine::detectBoardType();
        log_info("Detected board: " << Machine::getBoardTypeName(boardType));
        
        // Setup input polling loop after loading the configuration,
        // because the polling may depend on the config
        allChannels.init();

        WebUI::WiFiConfig::reset();

        display_init();

        protocol_init();

        // Load settings from non-volatile storage
        settings_init();  // requires config

        log_info("FluidNC " << git_info);
        log_info("Compiled with ESP32 SDK:" << esp_get_idf_version());

        bool configOkay = config->loadLayered();

        make_user_commands();

        if (configOkay) {
            log_info("Machine " << config->_name);
            log_info("Board " << config->_board);

            // The initialization order reflects dependencies between the subsystems
            for (size_t i = 1; i < MAX_N_UARTS; i++) {
                if (config->_uarts[i]) {
                    config->_uarts[i]->begin();
                }
            }
            for (size_t i = 1; i < MAX_N_UARTS; i++) {
                if (config->_uart_channels[i]) {
                    config->_uart_channels[i]->init();
                }
            }

            for (size_t i = 0; i < MAX_N_I2C; i++) {
                if (config->_i2c[i]) {
                    config->_i2c[i]->init();
                }
            }

            if (config->_i2so) {
                config->_i2so->init();
            }
            if (config->_spi) {
                config->_spi->init();
#ifndef USE_SDMMC
                if (config->_sdCard != nullptr) {
                    config->_sdCard->init();
                }
#endif
            }
#ifdef USE_SDMMC
            if (config->_sdCard != nullptr) {
                config->_sdCard->init();
            }
#endif
            // We have to initialize the extenders first, before pins are used
            if (config->_extenders) {
                config->_extenders->init();
            }

            if (config->_oled) {
                config->_oled->init();
            }

            if (config->_encoder) {
                config->_encoder->init();
            }

            if (config->_ultrasonic) {
                config->_ultrasonic->init();
            }

            config->_stepping->init();  // Configure stepper interrupt timers

            plan_init();

            config->_userOutputs->init();

            config->_axes->init();

            // Rebuild menu after axes are fully initialized
            if (config->_oled && config->_oled->_menu) {
                config->_oled->_menu->rebuild();
            }

            config->_control->init();

            config->_kinematics->init();
        } else { // Things we want to initialize even if no config.
            // If we're here, recovery config loaded but user config failed
            // Initialize critical UI components for error recovery
            
            // I2C bus (required for OLED)
            for (size_t i = 0; i < MAX_N_I2C; i++) {
                if (config->_i2c[i]) {
                    config->_i2c[i]->init();
                }
            }
            
            // OLED display (for error messages)
            if (config->_oled) {
                config->_oled->init();
            }
            
            // Encoder (for menu navigation)
            if (config->_encoder) {
                config->_encoder->init();
            }
            
            // Control pins (enter button for recovery)
            if (config->_control) {
                config->_control->init();
            }
            
            log_info("Critical UI components initialized for error recovery");
        }

        // Initialize system state.
        if (sys.state != State::ConfigAlarm) {
            if (FORCE_INITIALIZATION_ALARM) {
                // Force ALARM state upon a power-cycle or hard reset.
                sys.state = State::Alarm;
            } else {
                sys.state = State::Idle;
            }

            limits_init();

            // Check for power-up and set system alarm if homing is enabled to force homing cycle
            // by setting alarm state. Alarm locks out all g-code commands, including the
            // startup scripts, but allows access to settings and internal commands. Only a homing
            // cycle '$H' or kill alarm locks '$X' will disable the alarm.
            // NOTE: The startup script will run after successful completion of the homing cycle, but
            // not after disabling the alarm locks. Prevents motion startup blocks from crashing into
            // things uncontrollably. Very bad.
            if (config->_start->_mustHome && config->_axes->hasRealHomingCycles()) {
                // If there is an axis with real homing configured, enter Alarm state on startup
                sys.state = State::Alarm;
            } else if (!config->_axes->hasRealHomingCycles()) {
                // No axes have real homing cycles - mark as already homed
                config->_axes->_homed = true;
            }
            for (auto s : config->_spindles) {
                s->init();
            }
            Spindles::Spindle::switchSpindle(0, config->_spindles, spindle);

            config->_coolant->init();
            config->_probe->init();
        }

    } catch (const AssertionFailed& ex) {
        // This means something is terribly broken:
        log_error("Critical error in main_init: " << ex.what());
        sys.state = State::ConfigAlarm;
    }

    // Derive hostname from config name if config changed
    derive_hostname_from_config();

    log_startup_resources("Pre-radio");

    // Try Bluetooth first so its memory can be released if it is disabled
    if (!WebUI::bt_config.begin()) {
        WebUI::wifi_config.begin();
    }

    log_startup_resources("Post-radio");

    // Rebuild settings menu now that WiFi state is known
    if (config->_oled && config->_oled->_menu) {
        config->_oled->_menu->rebuild_settings_menu();
    }

    // Clear WiFi status popup so OLED resumes normal display
    if (config && config->_oled) {
        config->_oled->clear_popup();
    }

    allChannels.deregistration(&startupLog);
}

static void reset_variables() {
    // Reset primary systems.
    system_reset();
    protocol_reset();
    gc_init();  // Set g-code parser to default state
    // Spindle should be set either by the configuration
    // or by the post-configuration fixup, but we test
    // it anyway just for safety.  We want to avoid any
    // possibility of crashing at this point.

    plan_reset();  // Clear block buffer and planner variables

    if (sys.state != State::ConfigAlarm) {
        if (spindle) {
            spindle->stop();
            report_ovr_counter = 0;  // Set to report change immediately
        }
        Stepper::reset();  // Clear stepper subsystem variables
    }

    // Sync cleared gcode and planner positions to current system position.
    plan_sync_position();
    gc_sync_position();
    allChannels.flushRx();
    report_init_message(allChannels);
    mc_init();
}

void loop() {
    static int tries = 0;
    try {
        log_startup_resources("Pre-main-loop");
        reset_variables();

#ifdef DEBUG_MEMORY
        // DEBUG: Start memory management task 
        xTaskCreate(mem_task, "mem_task", 4096, NULL, 3, NULL);
#endif
        // Start the main loop. Processes program inputs and executes them.
        // This can exit on a system abort condition, in which case run_once()
        // is re-executed by an enclosing loop.  It can also exit via a
        // throw that is caught and handled below.
        protocol_main_loop();
    } catch (const AssertionFailed& ex) {
        // If an assertion fails, we display a message and restart.
        // This could result in repeated restarts if the assertion
        // happens before waiting for input, but that is unlikely
        // because the code in reset_variables() and the code
        // that precedes the input loop has few configuration
        // dependencies.  The safest approach would be to set
        // a "reconfiguration" flag and redo the configuration
        // step, but that would require combining main_init()
        // and run_once into a single control flow, and it would
        // require careful teardown of the existing configuration
        // to avoid memory leaks. It is probably worth doing eventually.
        log_error("Critical error in run_once: " << ex.msg);
        log_error("Stacktrace: " << ex.stackTrace);
        sys.state = State::ConfigAlarm;
    }
    // sys.abort is a user-initiated exit via ^x so we don't limit the number of occurrences
    if (!sys.abort && ++tries > 1) {
        log_info("Stalling due to too many failures");
        while (1) {}
    }
}

void WEAK_LINK machine_init() {}

#    if 0
int main() {
    setup();  // setup()
    while (1) {   // loop()
        loop();
    }
    return 0;
}
#    endif

#endif
