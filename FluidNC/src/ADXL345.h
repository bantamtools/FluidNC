#ifndef ADXL345_h
#define ADXL345_h

#include "Arduino.h"
#include <Wire.h>
#include "Machine/I2CBus.h"

using namespace Machine;

// Axis types
typedef enum {
    ACCEL_X_AXIS = 0,
    ACCEL_Y_AXIS,
    ACCEL_Z_AXIS,
} accel_axis_t;

// Register Map
// Order is "Type, Reset Value, Description".
#define ADXL345_REG_DEVID               0x00    // R,     11100101,   Device ID
#define ADXL345_REG_THRESH_TAP          0x1D    // R/W,   00000000,   Tap threshold
#define ADXL345_REG_OFSX                0x1E    // R/W,   00000000,   X-axis offset
#define ADXL345_REG_OFSY                0x1F    // R/W,   00000000,   Y-axis offset
#define ADXL345_REG_OFSZ                0x20    // R/W,   00000000,   Z-axis offset
#define ADXL345_REG_DUR                 0x21    // R/W,   00000000,   Tap duration
#define ADXL345_REG_LATENT              0x22    // R/W,   00000000,   Tap latency 
#define ADXL345_REG_WINDOW              0x23    // R/W,   00000000,   Tap window
#define ADXL345_REG_THRESH_ACT          0x24    // R/W,   00000000,   Activity threshold
#define ADXL345_REG_THRESH_INACT        0x25    // R/W,   00000000,   Inactivity threshold
#define ADXL345_ADXL345_REG_TIME_INACT  0x26    // R/W,   00000000,   Inactivity time
#define ADXL345_REG_ACT_INACT_CTL       0x27    // R/W,   00000000,   Axis enable control for activity and inactiv ity detection
#define ADXL345_REG_THRESH_FF           0x28    // R/W,   00000000,   Free-fall threshold
#define ADXL345_REG_TIME_FF             0x29    // R/W,   00000000,   Free-fall time
#define ADXL345_REG_TAP_AXES            0x2A    // R/W,   00000000,   Axis control for single tap/double tap
#define ADXL345_REG_ACT_TAP_STATUS      0x2B    // R,     00000000,   Source of single tap/double tap
#define ADXL345_REG_BW_RATE             0x2C    // R/W,   00001010,   Data rate and power mode control
#define ADXL345_REG_POWER_CTL           0x2D    // R/W,   00000000,   Power-saving features control
#define ADXL345_REG_INT_ENABLE          0x2E    // R/W,   00000000,   Interrupt enable control
#define ADXL345_REG_INT_MAP             0x2F    // R/W,   00000000,   Interrupt mapping control
#define ADXL345_REG_INT_SOUCE           0x30    // R,     00000010,   Source of interrupts
#define ADXL345_REG_DATA_FORMAT         0x31    // R/W,   00000000,   Data format control
#define ADXL345_REG_DATAX0              0x32    // R,     00000000,   X-Axis Data 0
#define ADXL345_REG_DATAX1              0x33    // R,     00000000,   X-Axis Data 1
#define ADXL345_REG_DATAY0              0x34    // R,     00000000,   Y-Axis Data 0
#define ADXL345_REG_DATAY1              0x35    // R,     00000000,   Y-Axis Data 1
#define ADXL345_REG_DATAZ0              0x36    // R,     00000000,   Z-Axis Data 0
#define ADXL345_REG_DATAZ1              0x37    // R,     00000000,   Z-Axis Data 1
#define ADXL345_REG_FIFO_CTL            0x38    // R/W,   00000000,   FIFO control
#define ADXL345_REG_FIFO_STATUS         0x39    // R,     00000000,   FIFO status

// Data Rate
#define ADXL345_RATE_3200HZ             0x0F    // 3200 Hz
#define ADXL345_RATE_1600HZ             0x0E    // 1600 Hz
#define ADXL345_RATE_800HZ              0x0D    // 800 Hz
#define ADXL345_RATE_400HZ              0x0C    // 400 Hz
#define ADXL345_RATE_200HZ              0x0B    // 200 Hz
#define ADXL345_RATE_100HZ              0x0A    // 100 Hz
#define ADXL345_RATE_50HZ               0x09    // 50 Hz
#define ADXL345_RATE_25HZ               0x08    // 25 Hz
#define ADXL345_RATE_12_5HZ             0x07    // 12.5 Hz
#define ADXL345_RATE_6_25HZ             0x06    // 6.25 Hz
#define ADXL345_RATE_3_13HZ             0x05    // 3.13 Hz
#define ADXL345_RATE_1_56HZ             0x04    // 1.56 Hz
#define ADXL345_RATE_0_78HZ             0x03    // 0.78 Hz
#define ADXL345_RATE_0_39HZ             0x02    // 0.39 Hz
#define ADXL345_RATE_0_20HZ             0x01    // 0.20 Hz
#define ADXL345_RATE_0_10HZ             0x00    // 0.10 Hz

// Range
#define ADXL345_RANGE_2G                0x00    // +-2 g
#define ADXL345_RANGE_4G                0x01    // +-4 g
#define ADXL345_RANGE_8G                0x02    // +-8 g
#define ADXL345_RANGE_16G               0x03    // +-16 g

// Conversions
#define ADXL345_GRAVITY_STD             (9.80665F) // Earth's gravity in m/s^2

class ADXL345 {
  private:
    class PowerCtlBits {
      public:
        uint8_t link;       // D5
        uint8_t auto_sleep; // D4
        uint8_t measure;    // D3
        uint8_t sleep;      // D2
        uint8_t wakeup;     // D1 - D0

        PowerCtlBits();

        uint8_t to_byte();
    };

    class DataFormatBits {
      public:
        uint8_t self_test;  // D7
        uint8_t spi;        // D6
        uint8_t int_invert; // D5
        uint8_t full_res;   // D3
        uint8_t justify;    // D2
        uint8_t range;      // D1 - D0

        DataFormatBits();

        uint8_t to_byte();
    };

    class BwRateBits {
      public:
        uint8_t low_power;  // D4
        uint8_t rate;       // D3 - D0

        BwRateBits();

        uint8_t to_byte();
    };

    static constexpr float k_ratio_2g = (float) (2 * 2) / 1024.0f;
    static constexpr float k_ratio_4g = (float) (4 * 2) / 1024.0f;
    static constexpr float k_ratio_8g = (float) (8 * 2) / 1024.0f;
    static constexpr float k_ratio_16g = (float) (16 * 2) / 1024.0f;

    uint8_t _address;
    I2CBus* _i2c;
    int16_t _xyz[3];
    PowerCtlBits _power_ctl_bits;
    DataFormatBits _data_format_bits;
    BwRateBits _bw_rate_bits;

    float convertToG(int16_t raw_value);
    float convertToMetersPerSec2(int16_t raw_value);

    bool write(uint8_t value);
    bool write(uint8_t *values, size_t size);
    bool read(uint8_t *values, int size);
    bool read_register(uint8_t address, uint8_t *value);
    bool read_registers(uint8_t address, uint8_t *values, uint8_t size);
    bool write_register(uint8_t address, uint8_t value);

  public:
    ADXL345(uint8_t address, I2CBus* i2c);
    bool start();
    bool stop();
    uint8_t read_device_id();
    bool update();
    int16_t get_raw(accel_axis_t axis);
    float get_m_per_sec2(accel_axis_t axis);
    float get_gs(accel_axis_t axis);

    bool write_rate(uint8_t rate);
    bool write_rate_with_low_power(uint8_t rate);
    bool write_range(uint8_t range);
};

#endif
