// Copyright (c) 2023 -  Matt Staniszewski, Bantam Tools

#include "Extenders.h"
#include "TCA6408.h"
#include "../Logging.h"

#include <esp32-hal-gpio.h>
#include <freertos/FreeRTOS.h>

namespace Extenders {
    void TCA6408::claim(pinnum_t index) {
        Assert(index >= 0 && index < 8, "TCA6408 IO index should be [0-7]; %d is out of range", index);

        uint32_t mask = uint32_t(1) << index;
        Assert((_claimed & mask) == 0, "TCA6408 IO port %d is already used.", index);

        _claimed |= mask;
    }

    void TCA6408::free(pinnum_t index) {
        uint32_t mask = uint32_t(1) << index;
        _claimed &= ~mask;
    }
//MS: stop
    uint8_t TCA6408::I2CGetValue(Machine::I2CBus* bus, uint8_t address, uint8_t reg) {
        auto err = bus->write(address, &reg, 1);

        if (err) {
            // log_info("Error writing to i2c bus. Code: " << err);
            return 0;
        }

        uint8_t inputData;
        if (bus->read(address, &inputData, 1) != 1) {
            // log_info("Error reading from i2c bus.");
        }

        return inputData;
    }

    void TCA6408::I2CSetValue(Machine::I2CBus* bus, uint8_t address, uint8_t reg, uint8_t value) {
        uint8_t data[2];
        data[0]  = reg;
        data[1]  = uint8_t(value);
        auto err = bus->write(address, data, 2);

        if (err) {
            log_error("Error writing to i2c bus; TCA6408 failed. Code: " << err);
        }
    }

    void TCA6408::validate() {
        auto i2c = config->_i2c[config->_extenders->_i2c_num];
        Assert(i2c != nullptr, "TCA6408 works through I2C, but I2C is not configured.");
    }

    void TCA6408::group(Configuration::HandlerBase& handler) {
        handler.item("interrupt0", _isrData[0]._pin);
        handler.item("interrupt1", _isrData[1]._pin);
        handler.item("interrupt2", _isrData[2]._pin);
        handler.item("interrupt3", _isrData[3]._pin);
    }

    void TCA6408::isrTaskLoop(void* arg) {
        auto inst = static_cast<TCA6408*>(arg);
        while (true) {
            void* ptr;
            if (xQueueReceive(inst->_isrQueue, &ptr, portMAX_DELAY)) {
                ISRData* valuePtr = static_cast<ISRData*>(ptr);
                // log_info("PCA state change ISR");
                valuePtr->updateValueFromDevice();
            }
        }
    }

    void TCA6408::init() {
        this->_i2cBus = config->_i2c[config->_extenders->_i2c_num];

        _isrQueue = xQueueCreate(16, sizeof(void*));
        xTaskCreatePinnedToCore(isrTaskLoop,                     // task
                                "isr_handler",                   // name for task
                                configMINIMAL_STACK_SIZE + 256,  // size of task stack
                                this,                            // parameters
                                1,                               // priority
                                &_isrHandler,
                                SUPPORT_TASK_CORE  // core
        );

        for (int i = 0; i < 4; ++i) {
            auto& data = _isrData[i];

            data._address   = uint8_t(0x20 + i);
            data._container = this;
            data._valueBase = reinterpret_cast<volatile uint16_t*>(&_value) + i;

            // Update the value first by reading it:
            data.updateValueFromDevice();

            if (!data._pin.undefined()) {
                data._pin.setAttr(Pin::Attr::ISR | Pin::Attr::Input);

                // The interrupt pin is 'active low'. So if it falls, we're interested in the new value.
                data._pin.attachInterrupt(updatePCAState, FALLING, &data);
            } else {
                // Reset valueBase so we know it's not bound to an ISR:
                data._valueBase = nullptr;
            }
        }
    }

    void TCA6408::ISRData::updateValueFromDevice() {
        const uint8_t InputReg = 0;
        auto          i2cBus   = _container->_i2cBus;

        auto     r1       = I2CGetValue(i2cBus, _address, InputReg);
        auto     r2       = I2CGetValue(i2cBus, _address, InputReg + 1);
        uint16_t oldValue = *_valueBase;
        uint16_t value    = (uint16_t(r2) << 8) | uint16_t(r1);
        *_valueBase       = value;

        if (_hasISR) {
            for (int i = 0; i < 16; ++i) {
                uint16_t mask = uint16_t(1) << i;

                if (_isrCallback[i] != nullptr && (oldValue & mask) != (value & mask)) {
                    // log_info("State change pin " << i);
                    switch (_isrMode[i]) {
                        case RISING:
                            if ((value & mask) == mask) {
                                _isrCallback[i](_isrArgument);
                            }
                            break;
                        case FALLING:
                            if ((value & mask) == 0) {
                                _isrCallback[i](_isrArgument);
                            }
                            break;
                        case CHANGE:
                            _isrCallback[i](_isrArgument);
                            break;
                    }
                }
            }
        }
    }

    void TCA6408::updatePCAState(void* ptr) {
        ISRData* valuePtr = static_cast<ISRData*>(ptr);

        BaseType_t xHigherPriorityTaskWoken = false;
        xQueueSendFromISR(valuePtr->_container->_isrQueue, &valuePtr, &xHigherPriorityTaskWoken);
    }

    void TCA6408::setupPin(pinnum_t index, Pins::PinAttributes attr) {
        bool activeLow = attr.has(Pins::PinAttributes::ActiveLow);
        bool output    = attr.has(Pins::PinAttributes::Output);

        uint32_t mask  = uint32_t(1) << index;
        _invert        = (_invert & ~mask) | (activeLow ? mask : 0);
        _configuration = (_configuration & ~mask) | (output ? 0 : mask);

        const uint8_t deviceId = index / 16;

        const uint8_t ConfigReg = 6;
        uint8_t       address   = 0x20 + deviceId;

        uint8_t value = uint8_t(_configuration >> (8 * (index / 8)));
        uint8_t reg   = ConfigReg + ((index / 8) & 1);

        // log_info("Setup reg " << int(reg) << " with value " << int(value));

        I2CSetValue(_i2cBus, address, reg, value);
    }

    void TCA6408::writePin(pinnum_t index, bool high) {
        uint32_t mask   = uint32_t(1) << index;
        uint32_t oldVal = _value;
        uint32_t newVal = high ? mask : uint32_t(0);
        _value          = (_value & ~mask) | newVal;

        _dirtyRegisters |= ((_value != oldVal) ? 1 : 0) << (index / 8);
    }

    bool TCA6408::readPin(pinnum_t index) {
        uint8_t reg      = uint8_t(index / 8);
        uint8_t deviceId = reg / 2;

        // If it's handled by the ISR, we don't need to read anything from the device.
        // Otherwise, we do. Check:
        if (_isrData[deviceId]._valueBase == nullptr) {
            const uint8_t InputReg = 0;
            uint8_t       address  = 0x20 + deviceId;

            auto     readReg  = InputReg + (reg & 1);
            auto     value    = I2CGetValue(_i2cBus, address, readReg);
            uint32_t newValue = uint32_t(value) << (int(reg) * 8);
            uint32_t mask     = uint32_t(0xff) << (int(reg) * 8);

            _value = ((newValue ^ _invert) & mask) | (_value & ~mask);

            // log_info("Read reg " << int(readReg) << " <- value " << int(newValue) << " gives " << int(_value));
        }
        // else {
        //     log_info("No read, value is " << int(_value));
        // }

        return (_value & (1ull << index)) != 0;
    }

    void TCA6408::flushWrites() {
        uint32_t write = _value ^ _invert;
        for (int i = 0; i < 8; ++i) {
            if ((_dirtyRegisters & (1 << i)) != 0) {
                const uint8_t OutputReg = 2;
                uint8_t       address   = 0x20 + (i / 2);

                uint8_t val = uint8_t(write >> (8 * i));
                uint8_t reg = OutputReg + (i & 1);
                I2CSetValue(_i2cBus, address, reg, val);
            }
        }

        _dirtyRegisters = 0;
    }

    // ISR's:
    void TCA6408::attachInterrupt(pinnum_t index, void (*callback)(void*), void* arg, int mode) {
        int device    = index / 16;
        int pinNumber = index % 16;

        Assert(_isrData[device]._isrCallback[pinNumber] == nullptr, "You can only set a single ISR for pin %d", index);

        _isrData[device]._isrCallback[pinNumber] = callback;
        _isrData[device]._isrArgument[pinNumber] = arg;
        _isrData[device]._isrMode[pinNumber]     = mode;
        _isrData[device]._hasISR                 = true;
    }

    void TCA6408::detachInterrupt(pinnum_t index) {
        int device    = index / 16;
        int pinNumber = index % 16;

        _isrData[device]._isrCallback[pinNumber] = nullptr;
        _isrData[device]._isrArgument[pinNumber] = nullptr;
        _isrData[device]._isrMode[pinNumber]     = 0;

        bool hasISR = false;
        for (int i = 0; i < 16; ++i) {
            hasISR |= (_isrData[device]._isrArgument[i] != nullptr);
        }
        _isrData[device]._hasISR = hasISR;
    }

    const char* TCA6408::name() const { return "TCA6408"; }

    TCA6408 ::~TCA6408() {
        for (int i = 0; i < 4; ++i) {
            auto& data = _isrData[i];

            if (!data._pin.undefined()) {
                data._pin.detachInterrupt();
            }
        }
    }

    // Register extender:
    namespace {
        PinExtenderFactory::InstanceBuilder<TCA6408> registration("TCA6408");
    }
}
