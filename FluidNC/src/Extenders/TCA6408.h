// Copyright (c) 2023 -  Matt Staniszewski, Bantam Tools

#pragma once

#include "PinExtenderDriver.h"
#include "../Configuration/Configurable.h"
#include "../Machine/MachineConfig.h"
#include "../Machine/I2CBus.h"
#include "../Platform.h"

#include <bitset>

namespace Pins {
    class TCA6408PinDetail;
}

namespace Extenders {

    class TCA6408 : public PinExtenderDriver {
        friend class Pins::TCA6408PinDetail;

        // Address can be set for up to 2 devices. Each device supports 8 pins.

        static const int numberPins = 8;
        uint64_t         _claimed;

        Machine::I2CBus* _i2cBus;

        static uint8_t IRAM_ATTR I2CGetValue(Machine::I2CBus* bus, uint8_t address, uint8_t reg);
        static void IRAM_ATTR I2CSetValue(Machine::I2CBus* bus, uint8_t address, uint8_t reg, uint8_t value);

        // Registers:
        // 4x8 = 32 bits. Fits perfectly into an uint32.
        uint32_t          _configuration = 0;
        uint32_t          _invert        = 0;
        volatile uint32_t _value         = 0;

        // 2 devices, 2 registers per device. 8 bits is enough:
        uint8_t _dirtyRegisters = 0;

        QueueHandle_t _isrQueue   = nullptr;
        TaskHandle_t  _isrHandler = nullptr;

        static void isrTaskLoop(void* arg);

        struct ISRData {
            ISRData() = default;

            Pin                _pin;
            TCA6408*           _container = nullptr;
            volatile uint16_t* _valueBase = nullptr;
            uint8_t            _address   = 0;

            typedef void (*ISRCallback)(void*);

            bool        _hasISR          = false;
            ISRCallback _isrCallback[16] = { 0 };
            void*       _isrArgument[16] = { 0 };
            int         _isrMode[16]     = { 0 };

            void IRAM_ATTR updateValueFromDevice();
        };

        ISRData     _isrData[4];
        static void IRAM_ATTR updatePCAState(void* ptr);

    public:
        TCA6408() = default;

        void claim(pinnum_t index) override;
        void free(pinnum_t index) override;

        void validate() override;
        void group(Configuration::HandlerBase& handler) override;

        void init();

        void IRAM_ATTR setupPin(pinnum_t index, Pins::PinAttributes attr) override;
        void IRAM_ATTR writePin(pinnum_t index, bool high) override;
        bool IRAM_ATTR readPin(pinnum_t index) override;
        void IRAM_ATTR flushWrites() override;

        void attachInterrupt(pinnum_t index, void (*callback)(void*), void* arg, int mode) override;
        void detachInterrupt(pinnum_t index) override;

        const char* name() const override;

        ~TCA6408();
    };
}
