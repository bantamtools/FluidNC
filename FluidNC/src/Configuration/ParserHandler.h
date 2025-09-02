// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "HandlerBase.h"
#include "Parser.h"
#include "Configurable.h"
#include "../System.h"
#include "parser_logging.h"
#include "../Pins/ProtectedPinTracker.h"
#include "../string_util.h"

#include <vector>
#include <cstring>

namespace Configuration {
    class ParserHandler : public Configuration::HandlerBase {
    private:
        Configuration::Parser&   _parser;
        std::vector<const char*> _path;
        bool                     _overlayMode;

    public:
        void enterSection(const char* name, Configuration::Configurable* section) override {
            _path.push_back(name);  // For error handling

            // On entry, the token is for the section that invoked us.
            // We will handle following nodes with indents greater than entryIndent
            int entryIndent = _parser._token._indent;
            log_parser_verbose("Entered section " << name << " at indent " << entryIndent);

            // The next token controls what we do next.  If thisIndent is greater
            // than entryIndent, there are some subordinate tokens.
            _parser.Tokenize();
            int thisIndent = _parser._token._indent;
            log_parser_verbose("thisIndent " << _parser.key() << " " << thisIndent);

            // If thisIndent <= entryIndent, the section is empty - there are
            // no more-deeply-indented subordinate tokens.

            if (thisIndent > entryIndent) {
                // If thisIndent > entryIndent, the new token is the first token within
                // this section so we process tokens at the same level as thisIndent.
                for (; _parser._token._indent >= thisIndent; _parser.Tokenize()) {
                    log_parser_verbose(" KEY " << _parser.key() << " state " << int(_parser._token._state) << " indent "
                                               << _parser._token._indent);
                    if (_parser._token._indent > thisIndent) {
                        log_error("Skipping key " << _parser.key() << " indent " << _parser._token._indent << " this indent " << thisIndent);
                    } else {
                        log_parser_verbose("Parsing key " << _parser.key());
                        try {
                            section->group(*this);
                        } catch (const AssertionFailed& ex) {
                            if (sys.state != State::ConfigAlarm) {
                                // Log something meaningful to the user, but only once.
                                log_error("Configuration error at "; for (auto it : _path) { ss << '/' << it; } ss << ": " << ex.msg);

                                // Set the state to config alarm, so users can't run time machine.
                                sys.state = State::ConfigAlarm;
                            }
                            throw;  // Re-throw to abort all parsing
                        }

                        if (_parser._token._state == TokenState::Matching) {
                            log_warn("Ignored key " << _parser.key());
                        }
                        if (_parser._token._state == Configuration::TokenState::Matched) {
                            log_parser_verbose("Handled key " << _parser.key());
                        }
                    }
                }
            }

            // At this point we have the next token whose indent we
            // needed in order to decide what to do.  When we return,
            // the caller will call Tokenize() to get a token, so we
            // "hold" the current token so that Tokenize() will
            // release that token instead of parsing ahead.
            // _parser._token.held = true;

            _parser._token._state = TokenState::Held;
            log_parser_verbose("Left section at indent " << entryIndent << " holding " << _parser.key());

            _path.erase(_path.begin() + (_path.size() - 1));
        }

        bool matchesUninitialized(const char* name) override { return _parser.is(name); }

    public:
        ParserHandler(Configuration::Parser& parser, bool overlayMode = false) : _parser(parser), _overlayMode(overlayMode) {}

        void item(const char* name, int32_t& value, int32_t minValue, int32_t maxValue) override {
            if (_parser.is(name)) {
                value = _parser.intValue();
                constrain_with_message(value, minValue, maxValue, name);
            }
        }

        void item(const char* name, uint32_t& value, uint32_t minValue, uint32_t maxValue) override {
            if (_parser.is(name)) {
                value = _parser.uintValue();
                constrain_with_message(value, minValue, maxValue, name);
            }
        }

        void item(const char* name, int& value, EnumItem* e) override {
            if (_parser.is(name)) {
                value = _parser.enumValue(e);
            }
        }

        void item(const char* name, bool& value) override {
            if (_parser.is(name)) {
                value = _parser.boolValue();
            }
        }

        void item(const char* name, float& value, float minValue, float maxValue) override {
            if (_parser.is(name)) {
                value = _parser.floatValue();
                constrain_with_message(value, minValue, maxValue, name);
            }
        }

        void item(const char* name, std::vector<speedEntry>& value) override {
            if (_parser.is(name)) {
                value = _parser.speedEntryValue();
            }
        }

        void item(const char* name, UartData& wordLength, UartParity& parity, UartStop& stopBits) override {
            if (_parser.is(name)) {
                _parser.uartMode(wordLength, parity, stopBits);
            }
        }

        void item(const char* name, std::string& value, int minLength, int maxLength) override {
            if (_parser.is(name)) {
                value = _parser.stringValue();
            }
        }

        void item(const char* name, Pin& value) override {
            if (_parser.is(name)) {
                // Use static lookup table for critical pin names
                static const struct {
                    const char* name;
                    Pins::ProtectedPinTracker::PinFunction function;
                } criticalPins[] = {
                    {"a_pin",      Pins::ProtectedPinTracker::PinFunction::ENCODER_A},
                    {"b_pin",      Pins::ProtectedPinTracker::PinFunction::ENCODER_B},
                    {"enter_pin",  Pins::ProtectedPinTracker::PinFunction::ENTER_BUTTON},
                    {"sda_pin",    Pins::ProtectedPinTracker::PinFunction::I2C_SDA},
                    {"scl_pin",    Pins::ProtectedPinTracker::PinFunction::I2C_SCL},
                    {"clk_pin",    Pins::ProtectedPinTracker::PinFunction::SD_CLK},
                    {"cmd_pin",    Pins::ProtectedPinTracker::PinFunction::SD_CMD},
                    {"d0_pin",     Pins::ProtectedPinTracker::PinFunction::SD_D0},
                    {"d1_pin",     Pins::ProtectedPinTracker::PinFunction::SD_D1},
                    {"d2_pin",     Pins::ProtectedPinTracker::PinFunction::SD_D2},
                    {"d3_pin",     Pins::ProtectedPinTracker::PinFunction::SD_D3},
                    {"cd_pin",     Pins::ProtectedPinTracker::PinFunction::SD_CD},
                };
                
                // Default to OTHER (non-critical)
                Pins::ProtectedPinTracker::PinFunction requestedFunction = 
                    Pins::ProtectedPinTracker::PinFunction::OTHER;
                
                // Look up in table
                for (const auto& entry : criticalPins) {
                    if (strcmp(name, entry.name) == 0) {
                        requestedFunction = entry.function;
                        break;
                    }
                }
                
                // Bypass _parser.pinValue() and call Pin::create directly with function
                auto parsed = Pin::create(string_util::trim(_parser.stringValue()), requestedFunction);
                value.swap(parsed);
            }
        }

        void item(const char* name, IPAddress& value) override {
            if (_parser.is(name)) {
                value = _parser.ipValue();
            }
        }

        HandlerType handlerType() override { return HandlerType::Parser; }
        bool isOverlayMode() override { return _overlayMode; }
    };
}
