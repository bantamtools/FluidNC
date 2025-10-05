// AUTO-GENERATED FILE - DO NOT EDIT
// Generated from recover_hen.yaml and recover_rooster.yaml
// This file is included by BoardDetection.h

#pragma once

namespace Machine {
namespace CriticalPins {

    namespace Hen {
        constexpr int ENCODER_A = 35;
        constexpr int ENCODER_B = 48;
        constexpr int ENTER_PIN = 36;
        constexpr int I2C_FREQUENCY = 400000;
        constexpr int I2C_SCL = 40;
        constexpr int I2C_SDA = 41;
        constexpr int OLED_ADDRESS = 60;
        constexpr int SD_CD = 14;
        constexpr int SD_CLK = 10;
        constexpr int SD_CMD = 9;
        constexpr int SD_D0 = 8;
        constexpr int SD_D1 = 7;
        constexpr int SD_D2 = 13;
        constexpr int SD_D3 = 12;
        constexpr int SD_FREQUENCY = 5000000;
        constexpr int SD_WIDTH = 4;

        // C axis snippet for G4 dwell injection into user configs
        // This is NOT included in recovery YAML below to save flash space.
        // Recovery mode does not support G-code execution.
        static const char C_AXIS_YAML[] = R"YAML(  c:
    # C axis configuration (automatically added into the config file by the firmware)
    # The C axis is reserved for managing G4 dwell commands, which are treated as
    # moves in the sequence of G-code commands but do not move any physical motors.
    steps_per_mm: 1000
    max_rate_mm_per_min: 60
    acceleration_mm_per_sec2: 100000
    max_travel_mm: 10000000
    soft_limits: false
    homing:
      cycle: -1
    motor0:
      null_motor:

)YAML";

        // Full recovery YAML
        extern const char henRecoveryYAML[];
    }

    namespace Rooster {
        constexpr int ENCODER_A = 36;
        constexpr int ENCODER_B = 37;
        constexpr int ENTER_PIN = 38;
        constexpr int I2C_FREQUENCY = 400000;
        constexpr int I2C_SCL = 41;
        constexpr int I2C_SDA = 40;
        constexpr int IO_EXPANDER_INT = 35;
        constexpr int OLED_ADDRESS = 60;
        constexpr int SD_CD = 12;
        constexpr int SD_CLK = 5;
        constexpr int SD_CMD = 6;
        constexpr int SD_D0 = 2;
        constexpr int SD_D1 = 1;
        constexpr int SD_D2 = 11;
        constexpr int SD_D3 = 10;
        constexpr int SD_FREQUENCY = 5000000;
        constexpr int SD_WIDTH = 4;

        // C axis snippet for G4 dwell injection into user configs
        // This is NOT included in recovery YAML below to save flash space.
        // Recovery mode does not support G-code execution.
        static const char C_AXIS_YAML[] = R"YAML(  c:
    # C axis configuration (automatically added into the config file by the firmware)
    # The C axis is reserved for managing G4 dwell commands, which are treated as
    # moves in the sequence of G-code commands but do not move any physical motors.
    steps_per_mm: 1000
    max_rate_mm_per_min: 60
    acceleration_mm_per_sec2: 100000
    max_travel_mm: 10000000
    soft_limits: false
    homing:
      cycle: -1
    motor0:
      null_motor:

)YAML";

        // Full recovery YAML
        extern const char roosterRecoveryYAML[];
    }
}
}

// Recovery configuration YAML strings
const char* const henRecoveryYAML = R"YAML(name: "System Recovery (Hen)"
board: "Bantam Tools Serama Hen"
meta: "0.10.0"

start:
 must_home: false

control:
 enter_pin: gpio.36:low

# OLED; uses fixed I2C frequency of 800 kHz
i2c0:
   sda_pin: gpio.41
   scl_pin: gpio.40

oled:
   i2c_num: 0
   i2c_address: 60
   width: 128
   height: 64
   radio_delay_ms: 2000

# Encoder
encoder:
  a_pin: gpio.35
  b_pin: gpio.48

# Configure SD card in SDMMC mode
sdcard:
 frequency_hz: 5000000
 width: 4
 clk_pin: gpio.10
 cmd_pin: gpio.9
 d0_pin: gpio.8
 d1_pin: gpio.7
 d2_pin: gpio.13
 d3_pin: gpio.12
 cd_pin: gpio.14
)YAML";

const char* const roosterRecoveryYAML = R"YAML(name: "Recovery (Rooster)"
board: "Bantam Tools Serama Rooster"
meta: "1.0.0"

start:
 must_home: false

control:
 enter_pin: gpio.38:low

# OLED; uses fixed I2C frequency of 800 kHz
i2c0:
   sda_pin: gpio.40
   scl_pin: gpio.41

oled:
  i2c_num: 0
  i2c_address: 60
  width: 128
  height: 64
  radio_delay_ms: 2000

encoder:
 a_pin: gpio.36
 b_pin: gpio.37

# Configure SD card in SDMMC mode
sdcard:
 frequency_hz: 5000000
 width: 4
 clk_pin: gpio.5
 cmd_pin: gpio.6
 d0_pin: gpio.2
 d1_pin: gpio.1
 d2_pin: gpio.11
 d3_pin: gpio.10
 cd_pin: gpio.12

# IO Expander
extenders:
  i2c_num: 0
  pinextender0:
    i2c_extender:
      device: tca6408
      device_id: 0
      interrupt: gpio.35
)YAML";
