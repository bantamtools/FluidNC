#include "ADXL345.h"

ADXL345::ADXL345(uint8_t address, I2CBus* i2c) {
  _i2c = i2c;
  _address = address;

  _xyz[ACCEL_X_AXIS] = 0;
  _xyz[ACCEL_Y_AXIS] = 0;
  _xyz[ACCEL_Z_AXIS] = 0;
}

bool ADXL345::start() {
  _power_ctl_bits.measure = 1;
  return write_register(ADXL345_REG_POWER_CTL, _power_ctl_bits.to_byte());
}

bool ADXL345::stop() {
  _power_ctl_bits.measure = 0;
  return write_register(ADXL345_REG_POWER_CTL, _power_ctl_bits.to_byte());
}

uint8_t ADXL345::read_device_id() {
  uint8_t value = 0;
  if (read_register(ADXL345_REG_DEVID, &value)) {
    return value;
  } else {
    return 0;
  }
}

bool ADXL345::update() {
  // Read all axis registers (DATAX0, DATAX1, DATAY0, DATAY1, DATAZ0, DATAZ1) in accordance with data sheet.
  // "It is recommended that a multiple-uint8_t read of all registers be performed to prevent a change in data between reads of sequential registers."
  uint8_t values[6];
  if (read_registers(ADXL345_REG_DATAX0, values, sizeof(values))) {
    // convert endian
    _xyz[ACCEL_X_AXIS] = (int16_t) word(values[1], values[0]);  // x
    _xyz[ACCEL_Y_AXIS] = (int16_t) word(values[3], values[2]);  // y
    _xyz[ACCEL_Z_AXIS] = (int16_t) word(values[5], values[4]);  // z
    return true;
  } else {
    return false;
  }
}

int16_t ADXL345::get_raw(accel_axis_t axis) {
  return _xyz[axis];
}

float ADXL345::get_m_per_sec2(accel_axis_t axis) {
  return convertToMetersPerSec2(_xyz[axis]);
}

float ADXL345::get_gs(accel_axis_t axis) {
  return convertToG(_xyz[axis]);
}

bool ADXL345::write_rate(uint8_t rate) {
  _bw_rate_bits.low_power = 0;
  _bw_rate_bits.rate = rate & 0x0F;
  return write_register(ADXL345_REG_BW_RATE, _bw_rate_bits.to_byte());
}

bool ADXL345::write_rate_with_low_power(uint8_t rate) {
  _bw_rate_bits.low_power = 1;
  _bw_rate_bits.rate = rate & 0x0F;
  return write_register(ADXL345_REG_BW_RATE, _bw_rate_bits.to_byte());
}

bool ADXL345::write_range(uint8_t range) {
  _data_format_bits.range = range & 0x03;
  return write_register(ADXL345_REG_DATA_FORMAT, _data_format_bits.to_byte());
}

float ADXL345::convertToG(int16_t raw_value) {
  switch (_data_format_bits.range) {
    case ADXL345_RANGE_2G:
      return raw_value * k_ratio_2g;

    case ADXL345_RANGE_4G:
      return raw_value * k_ratio_4g;

    case ADXL345_RANGE_8G:
      return raw_value * k_ratio_8g;

    case ADXL345_RANGE_16G:
      return raw_value * k_ratio_16g;

    default:
      return 0;
  }
}

float ADXL345::convertToMetersPerSec2(int16_t raw_value) {
  switch (_data_format_bits.range) {
    case ADXL345_RANGE_2G:
      return raw_value * k_ratio_2g * ADXL345_GRAVITY_STD;

    case ADXL345_RANGE_4G:
      return raw_value * k_ratio_4g * ADXL345_GRAVITY_STD;

    case ADXL345_RANGE_8G:
      return raw_value * k_ratio_8g * ADXL345_GRAVITY_STD;

    case ADXL345_RANGE_16G:
      return raw_value * k_ratio_16g * ADXL345_GRAVITY_STD;

    default:
      return 0;
  }
}

bool ADXL345::write(uint8_t value) {
  uint8_t values[1] = {value};
  return write(values, 1);
}

bool ADXL345::write(uint8_t *values, size_t size) {

    if (_i2c->write(_address, values, size) < 0) {
        return false;
    }
    return true;
}

bool ADXL345::read(uint8_t *values, int size) {

    if (_i2c->read(_address, values, size) < 0) {
        return false;
    }
    return true;
}

bool ADXL345::read_register(uint8_t address, uint8_t *value) {
  return read_registers(address, value, 1);
}

bool ADXL345::read_registers(uint8_t address, uint8_t *values, uint8_t size) {
  if (!write(address)) {
    return false;
  }

  return read(values, size);
}

bool ADXL345::write_register(uint8_t address, uint8_t value) {
  uint8_t values[2] = {address, value};
  return write(values, sizeof(values));
}

ADXL345::PowerCtlBits::PowerCtlBits() {
  this->link        = 0;
  this->auto_sleep  = 0;
  this->measure     = 0;
  this->sleep       = 0;
  this->wakeup      = 0;
}

uint8_t ADXL345::PowerCtlBits::to_byte() {
  uint8_t bits = 0x00;
  bits |= this->link        << 5;
  bits |= this->auto_sleep  << 4;
  bits |= this->measure     << 3;
  bits |= this->sleep       << 2;
  bits |= this->wakeup;
  return bits;
}

ADXL345::DataFormatBits::DataFormatBits() {
  this->self_test   = 0;
  this->spi         = 0;
  this->int_invert  = 0;
  this->full_res    = 0;
  this->justify     = 0;
  this->range       = 0;
}

uint8_t ADXL345::DataFormatBits::to_byte() {
  uint8_t bits = 0x00;
  bits |= this->self_test   << 7;
  bits |= this->spi         << 6;
  bits |= this->int_invert  << 5;
  bits |= this->full_res    << 3;
  bits |= this->justify     << 2;
  bits |= this->range;
  return bits;
}

ADXL345::BwRateBits::BwRateBits() {
  this->low_power   = 0;
  this->rate        = 0x0A;
}

uint8_t ADXL345::BwRateBits::to_byte() {
  uint8_t bits = 0x00;
  bits |= this->low_power << 4;
  bits |= this->rate;
  return bits;
}
