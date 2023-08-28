// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#pragma once

#include <stdint.h>
#include <sstream>
#include <iomanip>
#include "Configuration/Configurable.h"
#include "Channel.h"
#include "ADXL345.h"

// Class
class Accelerometer : public Configuration::Configurable {

    uint8_t _i2c_address = 0x53;
    uint8_t _i2c_num = 1;

    Pin _int1_pin;  
    Pin _int2_pin;

    bool _error = false;

private:

    static constexpr UBaseType_t    ACCEL_READ_PRIORITY       = (configMAX_PRIORITIES - 3);
    static constexpr uint32_t       ACCEL_READ_STACK_SIZE     = 4096;
    static constexpr uint32_t       ACCEL_READ_PERIODIC_MS    = 100;


    static void read_task(void *pvParameters);

public:

    ADXL345* _accel;

	Accelerometer();
    ~Accelerometer();

    void init();
    bool is_active();
   
    // Configuration handlers
    void validate() override;
    void afterParse() override;
    void group(Configuration::HandlerBase& handler) override;
    
protected:

    bool _is_active = false;

};
