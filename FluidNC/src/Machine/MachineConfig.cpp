// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "MachineConfig.h"
#include "BoardDetection.h"

#include "DefaultConfig.h"

#include "../Kinematics/Kinematics.h"

// External declarations for recovery YAML strings (defined in BoardDetection_pins.inc)
extern const char* const henRecoveryYAML;
extern const char* const roosterRecoveryYAML;

#include "../Motors/MotorDriver.h"
#include "../Motors/NullMotor.h"

#include "../Spindles/NullSpindle.h"
#include "../UartChannel.h"

#include "../SettingsDefinitions.h"  // config_filename
#include "../FileStream.h"
#include "../../include/Driver/localfs.h"  // localfsName

#include "../Configuration/Parser.h"
#include "../Configuration/ParserHandler.h"
#include "../Configuration/Validator.h"
#include "../Configuration/AfterParse.h"
#include "../Configuration/ParseException.h"
#include "../Config.h"  // ENABLE_*
#include "../Pins/GPIOPinDetail.h"
#include "../Pin.h"
#include "../Pins/ProtectedPinTracker.h"

#include <cstdio>
#include <cstring>
#include <atomic>
#include <memory>
#include <fstream>
#include "../NutsBolts.h"  // for delay_ms

Machine::MachineConfig* config;

// Forward declaration for recovery function
void copyRecoveryConfigAndRestart();

// Recovery function for ConfigAlarm state
void copyRecoveryConfigAndRestart() {
    // Get board-specific recovery config
    Machine::BoardType board = Machine::getDetectedBoardType();
    const char* recoveryYaml = (board == Machine::BoardType::Rooster) ? 
                               roosterRecoveryYAML : henRecoveryYAML;
    
    // Determine filesystem path (same logic as Flashing::update_config_from_sdcard)
    std::string cfg_out_path;
    if (localfsName == spiffsName) {
        cfg_out_path = "/spiffs/config.yaml";
        log_info("Using spiffs for recovery config write");
    } else if (localfsName == littlefsName) {
        cfg_out_path = "/littlefs/config.yaml";
        log_info("Using littlefs for recovery config write");
    } else {
        log_error("Local FS is neither spiffs nor littlefs");
        config->_oled->clear_popup();
        config->_oled->show_persistent_msg(
            "ERROR: Unknown\nfilesystem type.\nCannot recover."
        );
        return;
    }
    
    // Write recovery config to filesystem with verification
    log_info("Writing recovery config to " << cfg_out_path);
    bool reformatted = false;  // Track if we've already tried reformatting
    
retry_write:
    // Step 1: Delete existing config (ignore errors)
    remove(cfg_out_path.c_str());
    
    // Step 2: Verify file is gone
    FILE* checkFile = fopen(cfg_out_path.c_str(), "r");
    if (checkFile) {
        fclose(checkFile);
        log_error("Failed to delete existing config");
        
        if (!reformatted) {
            log_info("Reformatting filesystem...");
            if (localfs_format(localfsName) == 0) {
                log_info("Filesystem reformatted successfully");
                reformatted = true;
                goto retry_write;  // Try entire sequence again
            } else {
                log_error("Failed to format filesystem");
            }
        }
        
        config->_oled->clear_popup();
        config->_oled->show_persistent_msg(
            "ERROR: Cannot delete old config.     Filesystem error."
        );
        return;
    }
    
    // Step 3: Write new config
    std::ofstream outputFile(cfg_out_path.c_str());
    if (!outputFile.is_open()) {
        log_error("Failed to open config for writing");
        
        if (!reformatted) {
            log_info("Reformatting filesystem...");
            if (localfs_format(localfsName) == 0) {
                log_info("Filesystem reformatted successfully");
                reformatted = true;
                goto retry_write;  // Try entire sequence again
            } else {
                log_error("Failed to format filesystem");
            }
        }
        
        config->_oled->clear_popup();
        config->_oled->show_persistent_msg(
            "ERROR: Cannot write recovery config. Filesystem error."
        );
        return;
    }
    
    outputFile << recoveryYaml;
    outputFile.close();
    
    // Step 4: Verify file exists
    checkFile = fopen(cfg_out_path.c_str(), "r");
    if (!checkFile) {
        log_error("Config write verification failed");
        
        if (!reformatted) {
            log_info("Reformatting filesystem...");
            if (localfs_format(localfsName) == 0) {
                log_info("Filesystem reformatted successfully");
                reformatted = true;
                goto retry_write;  // Try entire sequence again
            } else {
                log_error("Failed to format filesystem");
            }
        }
        
        config->_oled->clear_popup();
        config->_oled->show_persistent_msg(
            "ERROR: Config verify failed.         Filesystem error."
        );
        return;
    }
    fclose(checkFile);
    
    // Show restart message
    config->_oled->clear_popup();
    config->_oled->show_persistent_msg("Restarting...");
    
    // Small delay to ensure message is displayed and written to OLED
    delay_ms(250);
    
    log_info("Recovery config installed, restarting...");
    esp_restart();
}

// Print out reset reason at boot
#define DEBUG_RESET_REASON

// TODO FIXME: Split this file up into several files, perhaps put it in some folder and namespace Machine?

namespace Machine {
    void MachineConfig::group(Configuration::HandlerBase& handler) {
        handler.item("board", _board);
        handler.item("name", _name);
        handler.item("meta", _meta);

        handler.section("stepping", _stepping);

        handler.section("uart1", _uarts[1], 1);
        handler.section("uart2", _uarts[2], 2);

        handler.section("uart_channel1", _uart_channels[1]);
        handler.section("uart_channel2", _uart_channels[2]);

        handler.section("i2so", _i2so);

        handler.section("i2c0", _i2c[0], 0);
        handler.section("i2c1", _i2c[1], 1);

        handler.section("spi", _spi);
        handler.section("sdcard", _sdCard);

        handler.section("kinematics", _kinematics);
        handler.section("axes", _axes);

        handler.section("control", _control);
        handler.section("coolant", _coolant);
        handler.section("probe", _probe);
        handler.section("macros", _macros);
        handler.section("start", _start);
        handler.section("extenders", _extenders);
        handler.section("parking", _parking);

        handler.section("user_outputs", _userOutputs);

        handler.section("oled", _oled);

        handler.section("encoder", _encoder);

        handler.section("ultrasonic", _ultrasonic);

        Spindles::SpindleFactory::factory(handler, _spindles);

        // TODO: Consider putting these under a gcode: hierarchy level? Or motion control?
        handler.item("arc_tolerance_mm", _arcTolerance, 0.001, 1.0);
        handler.item("junction_deviation_mm", _junctionDeviation, 0.001, 1.0);
        handler.item("verbose_errors", _verboseErrors);
        handler.item("report_inches", _reportInches);
        handler.item("enable_parking_override_control", _enableParkingOverrideControl);
        handler.item("use_line_numbers", _useLineNumbers);
        handler.item("planner_blocks", _planner_blocks, 10, 120);
    }

    void MachineConfig::afterParse() {
        if (_axes == nullptr) {
            log_info("Axes: using defaults");
            _axes = new Axes();
        }

        if (_i2c[0] == nullptr) {
            log_info("I2C0: using defaults");
            _i2c[0] = new I2CBus(0);
        }
        

        if (_coolant == nullptr) {
            _coolant = new CoolantControl();
        }

        if (_kinematics == nullptr) {
            _kinematics = new Kinematics();
        }

        if (_probe == nullptr) {
            _probe = new Probe();
        }

        if (_userOutputs == nullptr) {
            _userOutputs = new UserOutputs();
        }

        if (_sdCard == nullptr) {
            _sdCard = new SDCard();
        }

        if (_spi == nullptr) {
            _spi = new SPIBus();
        }

        if (_stepping == nullptr) {
            _stepping = new Stepping();
        }

        // We do not auto-create an I2SO bus config node
        // Only if an i2so section is present will config->_i2so be non-null

        if (_control == nullptr) {
            _control = new Control();
        }

        if (_start == nullptr) {
            _start = new Start();
        }

        if (_parking == nullptr) {
            _parking = new Parking();
        }

        if (_spindles.size() == 0) {
            _spindles.push_back(new Spindles::Null());
        }

        if (_oled == nullptr) {
            _oled = new OLED();
        }

        if (_encoder == nullptr) {
            _encoder = new Encoder();
        }

        // Precaution in case the full spindle initialization does not happen
        // due to a configuration error
        spindle = _spindles[0];

        uint32_t next_tool = 100;
        for (auto s : _spindles) {
            if (s->_tool == -1) {
                s->_tool = next_tool++;
            }
        }

        if (_macros == nullptr) {
            _macros = new Macros();
        }

        // Determine machine type from name
        if (strncmp(_name.c_str(), "Bantam Tools EggBot", 19) == 0) {
            _machine_type = MachineType::EggBot;
        } else if (strncmp(_name.c_str(), "WaterColorBot", 13) == 0) {
            _machine_type = MachineType::WaterColorBot;
        }
        // Add future machine type checks here
        
    }

    // const char defaultConfig[] = "name: Default (Test Drive)\nboard: None\n";

    // LEGACY CODE - NOT USED - Replaced by loadLayered()
    // This method is kept commented for reference but is never called
    /*
    bool MachineConfig::load() {
        bool configOkay;
        // If the system crashes we skip the config file and use the default
        // builtin config.  This helps prevent reset loops on bad config files.
        esp_reset_reason_t reason = esp_reset_reason();

#ifdef DEBUG_RESET_REASON
        // Print out the reset reason
        delay_ms(500); // Let serial console catch up
        switch (reason) {
            case ESP_RST_UNKNOWN:   log_info("RESET: Reset reason can not be determined"); break;
            case ESP_RST_POWERON:   log_info("RESET: Reset due to power-on event"); break;
            case ESP_RST_EXT:       log_info("RESET: Reset by external pin (not applicable for ESP32)"); break;
            case ESP_RST_SW:        log_info("RESET: Software reset via esp_restart"); break;
            case ESP_RST_PANIC:     log_info("RESET: Software reset due to exception/panic"); break;
            case ESP_RST_INT_WDT:   log_info("RESET: Reset (software or hardware) due to interrupt watchdog"); break;
            case ESP_RST_TASK_WDT:  log_info("RESET: Reset due to task watchdog"); break;
            case ESP_RST_WDT:       log_info("RESET: Reset due to other watchdogs"); break;
            case ESP_RST_DEEPSLEEP: log_info("RESET: Reset after exiting deep sleep mode"); break;
            case ESP_RST_BROWNOUT:  log_info("RESET: Brownout reset (software or hardware)"); break;
            case ESP_RST_SDIO:      log_info("RESET: Reset over SDIO"); break;
            default: break;
        }
#endif

        // Get the detected board type from early hardware detection
        BoardType detectedBoard = getDetectedBoardType();
        log_info("Config loading for detected " << getBoardTypeName(detectedBoard) << " board");

        if (reason == ESP_RST_PANIC) {
            log_error("Skipping configuration file due to panic");
            configOkay = false;
        } else {
            // Config filename is logged elsewhere if needed
            configOkay = load_file(config_filename->get(), detectedBoard);
        }
        
        if (!configOkay) {
            // Select recovery config based on detected board type
            log_debug("Recovery selection: detectedBoard = " << (int)detectedBoard << " (" << getBoardTypeName(detectedBoard) << ")");
            const char* recoveryYaml = nullptr;
            switch(detectedBoard) {
                case BoardType::Rooster:
                    log_info("Config load failed - using Rooster recovery configuration");
                    recoveryYaml = roosterRecoveryYAML;
                    break;
                default: //case BoardType::Hen:
                    log_info("Config load failed - using Hen recovery configuration");
                    recoveryYaml = henRecoveryYAML;
                    break;
                // default:
                //     log_info("Config load failed - using legacy default config");
                //     recoveryYaml = defaultConfig;
                //     break;
            }
            log_debug("Selected recovery YAML starts with: " << std::string(recoveryYaml, 0, 100));
            configOkay = load_yaml(recoveryYaml, detectedBoard);
        }


        return configOkay;
    }
    */

    bool MachineConfig::load_file(const std::string_view filename, BoardType detectedBoard) {
        return load_file(filename, detectedBoard, true);  // Default: clear pins
    }

    bool MachineConfig::load_file(const std::string_view filename, BoardType detectedBoard, bool clearPins) {
        try {
            FileStream file(std::string { filename }, "r", localfsName);
            auto filesize = file.size();
            if (filesize <= 0) {
                log_info("Configuration file:" << filename << " is empty");
                return false;
            }

            auto buffer      = std::make_unique<char[]>(filesize + 1);
            buffer[filesize] = '\0';
            auto actual      = file.read(buffer.get(), filesize);
            if (actual != filesize) {
                log_info("Configuration file:" << filename << " read error");
                return false;
            }
            // Trimming the overall config file could influence indentation, hence false
            return load_yaml(std::string_view { buffer.get(), filesize }, detectedBoard, clearPins);
        } catch (...) {
            log_warn("Cannot open configuration file:" << filename);
            return false;
        }
    }

    bool MachineConfig::loadLayered() {
        // Get board type early (already working in current system)
        BoardType detectedBoard = getDetectedBoardType();
        
        // Phase 1: ALWAYS load "recovery" (Critical UI) configuration first
        log_info("Loading baseline hardware config (from " << (detectedBoard == BoardType::Rooster ? "Rooster" : "Hen") << " \"recovery\" file)");
        
        const char* recoveryYaml = (detectedBoard == BoardType::Rooster) ? 
                                   roosterRecoveryYAML : henRecoveryYAML;
        
        if (!load_yaml(recoveryYaml, detectedBoard, true)) {  // clearPins = true
            log_error("Failed to load recovery configuration - system cannot continue");
            return false;  // Recovery config must succeed or system is unusable
        }
        
        // Phase 2: Mark critical systems immutable
        if (config->_i2c[0]) config->_i2c[0]->makeImmutable();
        if (config->_oled) config->_oled->makeImmutable();  
        if (config->_encoder) config->_encoder->makeImmutable();
        if (config->_control) config->_control->makeImmutable();
        if (config->_sdCard) config->_sdCard->makeImmutable();
        
        // Rooster-specific: Protect I/O expander
        if (detectedBoard == BoardType::Rooster && config->_extenders) {
            config->_extenders->makeImmutable();
        }
        
        // Protect critical pins as backup layer
        protectCriticalPins(detectedBoard);
        
        // Phase 3: Attempt to load user config as "overlay"
        const char* userConfigFile = config_filename->get();
        if (!userConfigFile || strlen(userConfigFile) == 0) {
            log_info("No user configuration specified - using recovery configuration only");
            return true;  // Recovery config alone provides full functionality
        }
        
        log_info("Loading user-selected config file: " << userConfigFile);
        
        // User config error handling - distinguish between fatal and warning errors
        try {
            bool userConfigOkay = load_file(userConfigFile, detectedBoard, false);  // clearPins = false
            if (userConfigOkay) {
                log_info("User configuration overlay applied successfully");
            } else {
                // Check if failure was due to pin conflicts vs actual parse errors
                if (sys.state == State::ConfigAlarm) {
                    log_error("User configuration contains fatal errors - using recovery configuration only");
                    log_error("Critical systems remain functional but user config is rejected");
                    // Keep ConfigAlarm state to indicate config error
                } else {
                    // Actual parse failure
                    log_warn("User configuration failed to parse - using recovery configuration only");
                }
            }
        } catch (const Configuration::ParseException& ex) {
            log_error("FATAL: User configuration has syntax errors (line " << ex.LineNumber() << "): " << ex.What());
            log_error("User configuration rejected entirely - using recovery configuration only");
            sys.state = State::ConfigAlarm;  // Fatal error state
        } catch (const AssertionFailed& ex) {
            log_error("FATAL: User configuration assertion failed: " << ex.what());
            log_error("User configuration rejected entirely - using recovery configuration only");
            sys.state = State::ConfigAlarm;  // Fatal error state
        } catch (const std::exception& ex) {
            log_error("FATAL: User configuration unexpected error: " << ex.what());
            log_error("User configuration rejected entirely - using recovery configuration only");
            sys.state = State::ConfigAlarm;  // Fatal error state
        }
        
        // Only succeed if system is not in ConfigAlarm state due to corrupted config
        bool systemOkay = (sys.state != State::ConfigAlarm);
        if (!systemOkay) {
            log_error("System in ConfigAlarm - hardware initialization will be skipped");
        }
        return systemOkay;
    }

    bool MachineConfig::load_yaml(std::string_view input, BoardType detectedBoard) {
        return load_yaml(input, detectedBoard, true);  // Default: clear pins
    }

    bool MachineConfig::load_yaml(std::string_view input, BoardType detectedBoard, bool clearPins) {
        log_info("Full list of config file contents:\n" << input);

        bool successful = false;
        try {
            if (clearPins) {
                // Only clear pins for base layer (recovery config)
                Pins::GPIOPinDetail::clearAllClaims();
                Pins::ProtectedPinTracker::clear();
                MachineConfig::clearWarnings();
                log_debug("Pin allocation state cleared for base config");
                
                // Create new config instance
                auto& machineConfig = instance();
                if (machineConfig != nullptr) {
                    delete machineConfig;
                }
                machineConfig = new MachineConfig();
                config = instance();
                log_debug("New config instance created");
            } else {
                log_debug("Loading config overlay - preserving existing pin state");
            }
            
            // Parse the YAML configuration
            Configuration::Parser parser(input);
            Configuration::ParserHandler handler(parser, !clearPins);  // overlay mode when not clearing pins
            
            log_debug("Parsing configuration" << (clearPins ? " (base)" : " (overlay)"));
            handler.enterSection("machine", config);

            log_debug("Running after-parse tasks");
            try {
                Configuration::AfterParse afterParse;
                config->afterParse();
                config->group(afterParse);
            } catch (std::exception& ex) { 
                log_error("After-parse validation error: " << ex.what()); 
            }

            log_debug("Checking configuration");
            try {
                Configuration::Validator validator;
                config->validate();
                config->group(validator);
            } catch (std::exception& ex) { 
                log_error("Configuration validation error: " << ex.what()); 
            }

            successful = (sys.state != State::ConfigAlarm);

            if (!successful) {
                log_error("Configuration is invalid");
            } else {
                log_info("Baseline hardware configuration loaded successfully.");
            }

        } catch (const Configuration::ParseException& ex) {
            sys.state = State::ConfigAlarm;
            log_error("FATAL: Configuration parse error on line " << ex.LineNumber() << ": " << ex.What());
            if (!clearPins) {
                log_error("User configuration overlay rejected due to syntax errors");
                log_error("Config may be corrupted - system restart recommended");
                // For user config overlays, ParseException means the config is partially corrupted
                // The only safe option is to prevent hardware initialization entirely
            } else {
                log_error("Base configuration parse failed - cannot continue");
            }
            successful = false;
        } catch (const AssertionFailed& ex) {
            sys.state = State::ConfigAlarm;
            log_error("Configuration loading failed: " << ex.what());
            successful = false;
        } catch (std::exception& ex) {
            sys.state = State::ConfigAlarm;
            log_error("Configuration error: " << ex.what());
            successful = false;
        } catch (...) {
            sys.state = State::ConfigAlarm;
            log_error("Unknown error while processing config file");
            successful = false;
        }

        std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);
        return successful;
    }


    void MachineConfig::protectCriticalPins(BoardType detectedBoard) {
        using PinFunc = Pins::ProtectedPinTracker::PinFunction;

        if (detectedBoard == BoardType::Hen) {
            // Protect Hen critical pins
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Hen::I2C_SDA, PinFunc::I2C_SDA, "I2C SDA (Hen)");
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Hen::I2C_SCL, PinFunc::I2C_SCL, "I2C SCL (Hen)");
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Hen::ENCODER_A, PinFunc::ENCODER_A, "Encoder A (Hen)");
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Hen::ENCODER_B, PinFunc::ENCODER_B, "Encoder B (Hen)");
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Hen::ENTER_PIN, PinFunc::ENTER_BUTTON, "Enter Button (Hen)");
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Hen::SD_CLK, PinFunc::SD_CLK, "SD CLK (Hen)");
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Hen::SD_CMD, PinFunc::SD_CMD, "SD CMD (Hen)");
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Hen::SD_D0, PinFunc::SD_D0, "SD D0 (Hen)");
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Hen::SD_D1, PinFunc::SD_D1, "SD D1 (Hen)");
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Hen::SD_D2, PinFunc::SD_D2, "SD D2 (Hen)");
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Hen::SD_D3, PinFunc::SD_D3, "SD D3 (Hen)");
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Hen::SD_CD, PinFunc::SD_CD, "SD CD (Hen)");
        } else {
            // Protect Rooster critical pins
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Rooster::I2C_SDA, PinFunc::I2C_SDA, "I2C SDA (Rooster)");
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Rooster::I2C_SCL, PinFunc::I2C_SCL, "I2C SCL (Rooster)");
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Rooster::ENCODER_A, PinFunc::ENCODER_A, "Encoder A (Rooster)");
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Rooster::ENCODER_B, PinFunc::ENCODER_B, "Encoder B (Rooster)");
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Rooster::ENTER_PIN, PinFunc::ENTER_BUTTON, "Enter Button (Rooster)");
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Rooster::IO_EXPANDER_INT, PinFunc::IO_EXPANDER_INT, "I/O Expander Interrupt (Rooster)");
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Rooster::SD_CLK, PinFunc::SD_CLK, "SD CLK (Rooster)");
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Rooster::SD_CMD, PinFunc::SD_CMD, "SD CMD (Rooster)");
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Rooster::SD_D0, PinFunc::SD_D0, "SD D0 (Rooster)");
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Rooster::SD_D1, PinFunc::SD_D1, "SD D1 (Rooster)");
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Rooster::SD_D2, PinFunc::SD_D2, "SD D2 (Rooster)");
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Rooster::SD_D3, PinFunc::SD_D3, "SD D3 (Rooster)");
            Pins::ProtectedPinTracker::addProtectedPin(CriticalPins::Rooster::SD_CD, PinFunc::SD_CD, "SD CD (Rooster)");
        }
        // log_info("Critical pins protected for " << getBoardTypeName(detectedBoard) << " board");
    }

    MachineConfig::~MachineConfig() {
        delete _axes;
        delete _i2so;
        delete _coolant;
        delete _probe;
        delete _sdCard;
        delete _spi;
        delete _control;
        delete _macros;
    }

    // Warning counter system implementation
    int MachineConfig::configWarnings = 0;

    void MachineConfig::addWarning(const char* msg) {
        configWarnings++;
        log_warn(msg);
    }

    void MachineConfig::clearWarnings() {
        configWarnings = 0;
    }

    void MachineConfig::reportWarnings() {
        if (configWarnings > 0) {
            log_warn("Configuration loaded with " << configWarnings << " warnings");
            if (config && config->_oled) {
                config->_oled->setStartupConfigWarning();  // Reuse existing OLED flag
            }
        } else {
            log_info("Configuration loaded successfully - no issues detected");
        }
    }
}
