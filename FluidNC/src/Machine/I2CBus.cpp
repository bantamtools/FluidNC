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
        if (_immutable) {
            // Count warning and inform user
            Machine::MachineConfig::addWarning("I2C config ignored (using board defaults)");
            return;
        }
        handler.item("sda_pin", _sda);
        handler.item("scl_pin", _scl);
        // handler.item("frequency", _frequency); // Dont set display frequency from config.yaml.
    }

    void I2CBus::init() {
        _error = false;
        
        if (!_sda.defined() || !_scl.defined()) {
            log_debug("I2C bus " << _busNumber << " not configured - pins not defined");
            _error = true;
            return;
        }

        pinnum_t sdaPin = _sda.getNative(Pin::Capabilities::Native | Pin::Capabilities::Input | Pin::Capabilities::Output);
        pinnum_t sclPin = _scl.getNative(Pin::Capabilities::Native | Pin::Capabilities::Input | Pin::Capabilities::Output);

        _error = i2c_master_init(_busNumber, sdaPin, sclPin, _frequency);
        if (_error) {
            log_error("I2C init failed");
            return;
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
