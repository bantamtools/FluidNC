// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Config.h"

#include "SDCard.h"
#include "Machine/MachineConfig.h"
#include "Channel.h"
#include "Report.h"

#ifdef USE_SDMMC
#include "Driver/sdmmc.h"
#else
#include "Driver/sdspi.h"
#endif
#include "src/SettingsDefinitions.h"
#include "FluidPath.h"
#include "Protocol.h"

SDCard::SDCard() : _state(State::Idle) {}

void SDCard::init() {
#ifdef USE_SDMMC
    pinnum_t clkPin = -1, cmdPin = -1, d0Pin = -1, d1Pin = -1, d2Pin = -1, d3Pin = -1, cdPin = -1;
    static bool init_message = true;  // used to show messages only once.

    log_info("SD Card: freq = " << _frequency_hz << ", width = " << _width << ", clk = " << _clk.name() << ", cmd = " << _cmd.name() <<
             ", d0 = " << _d0.name() << ", d1 = " << _d1.name() << ", d2 = " << _d2.name() << ", d3 = " << _d3.name() << ", cd = " << _cd.name());
    init_message = false;

    // Configure the SDMMC clock/data pins
    _clk.setAttr(Pin::Attr::Output);
    _cmd.setAttr(Pin::Attr::Input | Pin::Attr::Output);

    _d0.setAttr(Pin::Attr::Input | Pin::Attr::Output);
    if (_d1.defined()) _d1.setAttr(Pin::Attr::Input | Pin::Attr::Output);
    if (_d2.defined()) _d2.setAttr(Pin::Attr::Input | Pin::Attr::Output);
    if (_d3.defined()) _d3.setAttr(Pin::Attr::Input | Pin::Attr::Output);
    if (_cd.defined()) _cd.setAttr(Pin::Attr::Input);

    clkPin = _clk.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
    cmdPin = _cmd.getNative(Pin::Capabilities::Input | Pin::Capabilities::Output | Pin::Capabilities::Native);
    d0Pin = _d0.getNative(Pin::Capabilities::Input | Pin::Capabilities::Output | Pin::Capabilities::Native);
    if (_d1.defined()) d1Pin = _d1.getNative(Pin::Capabilities::Input | Pin::Capabilities::Output | Pin::Capabilities::Native);
    if (_d2.defined()) d2Pin = _d2.getNative(Pin::Capabilities::Input | Pin::Capabilities::Output | Pin::Capabilities::Native);
    if (_d3.defined()) d3Pin = _d3.getNative(Pin::Capabilities::Input | Pin::Capabilities::Output | Pin::Capabilities::Native);
    if (_cd.defined()) cdPin = _cd.getNative(Pin::Capabilities::Input | Pin::Capabilities::Native);

    // Initialize the slot
    sd_init_slot(_frequency_hz, _width, clkPin, cmdPin, d0Pin, d1Pin, d2Pin, d3Pin, cdPin);

    // Set up an event pin with card detect actions
    CardDetectPin *cardDetectEventPin = new CardDetectPin(_cd);
    cardDetectEventPin->init();
#else
    static bool init_message = true;  // used to show messages only once.
    pinnum_t    csPin;
    int         csFallback;

    if (_cs.defined()) {
        if (!config->_spi->defined()) {
            log_error("SD needs SPI defined");
        } else {
            log_info("SD Card cs_pin:" << _cs.name() << " detect:" << _cardDetect.name() << " freq:" << _frequency_hz);
            init_message = false;
        }
        _cs.setAttr(Pin::Attr::Output);
        csPin = _cs.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
    } else if ((csFallback = sd_fallback_cs->get()) != -1) {
        csPin = static_cast<pinnum_t>(csFallback);
        log_info("Using fallback CS pin " << int(csPin));
    } else {
        log_debug("See http://wiki.fluidnc.com/en/config/sd_card#sdfallbackcs-access-sd-without-a-config-file");
        return;
    }

    if (_cardDetect.defined()) {
        _cardDetect.setAttr(Pin::Attr::Input);
        auto cdPin = _cardDetect.getNative(Pin::Capabilities::Input | Pin::Capabilities::Native);
        sd_init_slot(_frequency_hz, csPin, cdPin);
    } else {
        sd_init_slot(_frequency_hz, csPin);
    }

    // Set up an event pin with card detect actions
    CardDetectPin *cardDetectEventPin = new CardDetectPin(_cardDetect);
    cardDetectEventPin->init();
#endif
}

#ifdef USE_SDMMC
void SDCard::validate() {
    if (!_clk.defined() || !_cmd.defined() ) {
        Assert(_clk.defined(), "CLK pin should be configured once");
        Assert(_cmd.defined(), "CMD pin should be configured once");
    }
    if (_width != 1 && _width != 4) {
       Assert((_width == 1 || _width == 4), "WIDTH should be configured to 1 or 4"); 
    }
    if (_width == 1 && !_d0.defined()) {
       Assert(_d0.defined(), "D0 pin should be configured once"); 
    }
    if (_width == 4 && (!_d0.defined() || !_d1.defined() || !_d2.defined() || !_d3.defined())) {
       Assert(_d0.defined(), "D0 pin should be configured once"); 
       Assert(_d1.defined(), "D1 pin should be configured once"); 
       Assert(_d2.defined(), "D2 pin should be configured once"); 
       Assert(_d3.defined(), "D3 pin should be configured once"); 
    }
}
#endif

SDCard::~SDCard() {}
