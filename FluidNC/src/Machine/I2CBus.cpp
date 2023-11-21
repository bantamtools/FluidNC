// Copyright (c) 2022 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "I2CBus.h"
#include "Driver/fluidnc_i2c.h"

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
        _error      = false;

        // I2C0 not configured, default to LFP as fail-safe and test
        if (_busNumber == 0 && !_sda.defined() && !_scl.defined()) {

            // Start with LFP config and set flag
            _sda = Pin::create("gpio.40");
            _scl = Pin::create("gpio.41");
            _fail_safe = true;
        }

        auto sdaPin = _sda.getNative(Pin::Capabilities::Native | Pin::Capabilities::Input | Pin::Capabilities::Output);
        auto sclPin = _scl.getNative(Pin::Capabilities::Native | Pin::Capabilities::Input | Pin::Capabilities::Output);

        _error = i2c_master_init(_busNumber, sdaPin, sclPin, _frequency);
        if (_error) {
            log_error("I2C init failed");
        }

        // Check fail-safe, swtich to MVP if I2C extender doesn't exist (LFP only)
        uint8_t data = 0x00;
        if (_fail_safe && (write(0x20, &data, 1) < 0)) {

            // Uninitialize I2C0
            i2c_master_deinit(_busNumber);

            // Update the I2C pins for MVP fail-safe
            delete(&_scl);
            delete(&_sda);

            _sda = Pin::create("gpio.41");
            _scl = Pin::create("gpio.40");

            sdaPin = _sda.getNative(Pin::Capabilities::Native | Pin::Capabilities::Input | Pin::Capabilities::Output);
            sclPin = _scl.getNative(Pin::Capabilities::Native | Pin::Capabilities::Input | Pin::Capabilities::Output);
            
            _error = i2c_master_init(_busNumber, sdaPin, sclPin, _frequency);
            if (_error) {
                log_error("I2C init failed");
            }
        }

        // Set the MVP flag in I2C config for use in the system
        if (sdaPin == 41 && sclPin == 40) {
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
