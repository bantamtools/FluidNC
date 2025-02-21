// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Extenders.h"

namespace Extenders {
    PinExtender::PinExtender() : _driver(nullptr) {}

    void PinExtender::validate() {
        if (_driver) {
            _driver->validate();
        }
    }
    void PinExtender::group(Configuration::HandlerBase& handler) { PinExtenderFactory::factory(handler, _driver); }
    void PinExtender::init() {
        if (_driver) {
            _driver->init();
        }
    }

    PinExtender::~PinExtender() { delete _driver; }

    Extenders::Extenders() {
        for (int i = 0; i < 10; ++i) {
            _pinDrivers[i] = nullptr;
        }
    }

    void Extenders::validate() {}

    void Extenders::group(Configuration::HandlerBase& handler) {
        for (int i = 0; i < 10; ++i) {
            char tmp[11 + 3];
            tmp[0] = 0;
            strcat(tmp, "pinextender");

            tmp[11] = char(i + '0');
            tmp[12] = '\0';
            handler.section(tmp, _pinDrivers[i]);
        }
        handler.item("i2c_num", _i2c_num);
    }

    void Extenders::init() {
        for (int i = 0; i < 10; ++i) {
            if (_pinDrivers[i] != nullptr) {
                _pinDrivers[i]->init();
            }
        }
    }

    Extenders::~Extenders() {
        for (int i = 0; i < 10; ++i) {
            delete _pinDrivers[i];
        }
    }
}
