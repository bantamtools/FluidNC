#ifndef ADXL345_h
#define ADXL345_h

#include "Arduino.h"
#include <Wire.h>
#include "Machine/I2CBus.h"

using namespace Machine;

// Register Map
// Order is "Type, Reset Value, Description".
#define REG_DEVID          0x00    // R,     11100101,   Device ID
#define REG_THRESH_TAP     0x1D    // R/W,   00000000,   Tap threshold
#define REG_OFSX           0x1E    // R/W,   00000000,   X-axis offset
#define REG_OFSY           0x1F    // R/W,   00000000,   Y-axis offset
#define REG_OFSZ           0x20    // R/W,   00000000,   Z-axis offset
#define REG_DUR            0x21    // R/W,   00000000,   Tap duration
#define REG_LATENT         0x22    // R/W,   00000000,   Tap latency 
#define REG_WINDOW         0x23    // R/W,   00000000,   Tap window
#define REG_THRESH_ACT     0x24    // R/W,   00000000,   Activity threshold
#define REG_THRESH_INACT   0x25    // R/W,   00000000,   Inactivity threshold
#define REG_TIME_INACT     0x26    // R/W,   00000000,   Inactivity time
#define REG_ACT_INACT_CTL  0x27    // R/W,   00000000,   Axis enable control for activity and inactiv ity detection
#define REG_THRESH_FF      0x28    // R/W,   00000000,   Free-fall threshold
#define REG_TIME_FF        0x29    // R/W,   00000000,   Free-fall time
#define REG_TAP_AXES       0x2A    // R/W,   00000000,   Axis control for single tap/double tap
#define REG_ACT_TAP_STATUS 0x2B    // R,     00000000,   Source of single tap/double tap
#define REG_BW_RATE        0x2C    // R/W,   00001010,   Data rate and power mode control
#define REG_POWER_CTL      0x2D    // R/W,   00000000,   Power-saving features control
#define REG_INT_ENABLE     0x2E    // R/W,   00000000,   Interrupt enable control
#define REG_INT_MAP        0x2F    // R/W,   00000000,   Interrupt mapping control
#define REG_INT_SOUCE      0x30    // R,     00000010,   Source of interrupts
#define REG_DATA_FORMAT    0x31    // R/W,   00000000,   Data format control
#define REG_DATAX0         0x32    // R,     00000000,   X-Axis Data 0
#define REG_DATAX1         0x33    // R,     00000000,   X-Axis Data 1
#define REG_DATAY0         0x34    // R,     00000000,   Y-Axis Data 0
#define REG_DATAY1         0x35    // R,     00000000,   Y-Axis Data 1
#define REG_DATAZ0         0x36    // R,     00000000,   Z-Axis Data 0
#define REG_DATAZ1         0x37    // R,     00000000,   Z-Axis Data 1
#define REG_FIFO_CTL       0x38    // R/W,   00000000,   FIFO control
#define REG_FIFO_STATUS    0x39    // R,     00000000,   FIFO status

// I2C Address
#define ADXL345_STD     0x1D    // Standard address if SDO/ALT ADDRESS is HIGH.
#define ADXL345_ALT     0x53    // Alternate address if SDO/ALT ADDRESS is LOW.

// Data Rate
#define ADXL345_RATE_3200HZ   0x0F    // 3200 Hz
#define ADXL345_RATE_1600HZ   0x0E    // 1600 Hz
#define ADXL345_RATE_800HZ    0x0D    // 800 Hz
#define ADXL345_RATE_400HZ    0x0C    // 400 Hz
#define ADXL345_RATE_200HZ    0x0B    // 200 Hz
#define ADXL345_RATE_100HZ    0x0A    // 100 Hz
#define ADXL345_RATE_50HZ     0x09    // 50 Hz
#define ADXL345_RATE_25HZ     0x08    // 25 Hz
#define ADXL345_RATE_12_5HZ   0x07    // 12.5 Hz
#define ADXL345_RATE_6_25HZ   0x06    // 6.25 Hz
#define ADXL345_RATE_3_13HZ   0x05    // 3.13 Hz
#define ADXL345_RATE_1_56HZ   0x04    // 1.56 Hz
#define ADXL345_RATE_0_78HZ   0x03    // 0.78 Hz
#define ADXL345_RATE_0_39HZ   0x02    // 0.39 Hz
#define ADXL345_RATE_0_20HZ   0x01    // 0.20 Hz
#define ADXL345_RATE_0_10HZ   0x00    // 0.10 Hz

// Range
#define ADXL345_RANGE_2G      0x00    // +-2 g
#define ADXL345_RANGE_4G      0x01    // +-4 g
#define ADXL345_RANGE_8G      0x02    // +-8 g
#define ADXL345_RANGE_16G     0x03    // +-16 g

#define ADXL345_GRAVITY_STD   (9.80665F) // Earth's gravity in m/s^2

class ADXL345 {
  private:
    class PowerCtlBits {
      public:
        uint8_t link;       // D5
        uint8_t autoSleep;  // D4
        uint8_t measure;    // D3
        uint8_t sleep;      // D2
        uint8_t wakeup;     // D1 - D0

        PowerCtlBits();

        uint8_t toByte();
    };

    class DataFormatBits {
      public:
        uint8_t selfTest;   // D7
        uint8_t spi;        // D6
        uint8_t intInvert;  // D5
        uint8_t fullRes;    // D3
        uint8_t justify;    // D2
        uint8_t range;      // D1 - D0

        DataFormatBits();

        uint8_t toByte();
    };

    class BwRateBits {
      public:
        uint8_t lowPower;   // D4
        uint8_t rate;       // D3 - D0

        BwRateBits();

        uint8_t toByte();
    };

    static const float kRatio2g;
    static const float kRatio4g;
    static const float kRatio8g;
    static const float kRatio16g;

    uint8_t _address;
    I2CBus* _i2c;
    int16_t _xyz[3];
    PowerCtlBits _powerCtlBits;
    DataFormatBits _dataFormatBits;
    BwRateBits _bwRateBits;

    float convertToMetersPerSec2(int16_t rawValue);

    bool write(uint8_t value);
    bool write(uint8_t *values, size_t size);
    bool read(uint8_t *values, int size);
    bool readRegister(uint8_t address, uint8_t *value);
    bool readRegisters(uint8_t address, uint8_t *values, uint8_t size);
    bool writeRegister(uint8_t address, uint8_t value);

  public:
    ADXL345(uint8_t address, I2CBus* i2c);
    bool start();
    bool stop();
    uint8_t readDeviceID();
    bool update();
    float getX();
    float getY();
    float getZ();
    int16_t getRawX();
    int16_t getRawY();
    int16_t getRawZ();

    bool writeRate(uint8_t rate);
    bool writeRateWithLowPower(uint8_t rate);
    bool writeRange(uint8_t range);
};

#endif
