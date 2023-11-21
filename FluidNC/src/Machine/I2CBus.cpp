// Copyright (c) 2022 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "I2CBus.h"
#include "Driver/fluidnc_i2c.h"
#include "MachineConfig.h"

namespace Machine {
    I2CBus::I2CBus(int busNumber) : _busNumber(busNumber) {}

    void I2CBus::validate() {
        if (_sda.defined() || _scl.defined()) {
            Assert(_sda.defined(), "I2C SDA pin configured multiple times");
            Assert(_scl.defined(), "I2C SCL pin configured multiple times");
        }
    }

    void I2CBus::group(Configuration::HandlerBase& handler) {
        handler.item("sda_pin", _sda);
        handler.item("scl_pin", _scl);
        handler.item("frequency", _frequency);
    }

    void I2CBus::init() {
        
        pinnum_t sdaPin, sclPin;
        _error      = false;
        
        // I2C0 not configured, set fail-safe based on I2C detection
        if (_busNumber == 0 && !_sda.defined() && !_scl.defined()) {

            _fail_safe = true;
            
            // Start with LFP fail-safe config
            auto sdaPin = MachineConfig::FAILSAFE_LFP_I2C0_SDA_PIN;
            auto sclPin = MachineConfig::FAILSAFE_LFP_I2C0_SCL_PIN;

            // Check fail-safe, switch to MVP if I2C extender (LFP only) doesn't exist
            _error = i2c_master_init(_busNumber, sdaPin, sclPin, _frequency);
            if (_error) {
                log_error("I2C init failed");
            }

            // MVP config
            uint8_t data = 0x00;
            if (write(0x20, &data, 1) < 0) {
                _sda = Pin::create(MachineConfig::FAILSAFE_MVP_I2C0_SDA);
                _scl = Pin::create(MachineConfig::FAILSAFE_MVP_I2C0_SCL);
            
            // LFP config
            } else {
                _sda = Pin::create(MachineConfig::FAILSAFE_LFP_I2C0_SDA);
                _scl = Pin::create(MachineConfig::FAILSAFE_LFP_I2C0_SCL);
            }
            
            // Uninitialize I2C0
            i2c_master_deinit(_busNumber);
        }

        sdaPin = _sda.getNative(Pin::Capabilities::Native | Pin::Capabilities::Input | Pin::Capabilities::Output);
        sclPin = _scl.getNative(Pin::Capabilities::Native | Pin::Capabilities::Input | Pin::Capabilities::Output);

        _error = i2c_master_init(_busNumber, sdaPin, sclPin, _frequency);
        if (_error) {
            log_error("I2C init failed");
        }

        // Set the MVP flag in I2C config for use in the system
        if (sdaPin == MachineConfig::FAILSAFE_MVP_I2C0_SDA_PIN && sclPin == MachineConfig::FAILSAFE_MVP_I2C0_SCL_PIN) {
            _is_mvp = true;
            log_info("I2C0: using MVP configuration");   
        } else {
            _is_mvp = false;
            log_info("I2C0: using LFP configuration");   
        }

        log_info("I2C SDA: " << _sda.name() << ", SCL: " << _scl.name() << ", Freq: " << _frequency << ", Bus #: " << _busNumber);

    }

    int I2CBus::write(uint8_t address, const uint8_t* data, size_t count) {
        if (_error) {
            return -1;
        }
        return i2c_write(_busNumber, address, data, count);
    }

    int I2CBus::read(uint8_t address, uint8_t* data, size_t count) {
        if (_error) {
            return -1;
        }
        return i2c_read(_busNumber, address, data, count);
    }
}
