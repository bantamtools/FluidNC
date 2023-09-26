/*!
 *  @file BNO085.cpp
 *
 *  @mainpage Adafruit BNO08x 9-DOF Orientation IMU Fusion Breakout
 *
 *  @section intro_sec Introduction
 *
 * 	I2C Driver for the Library for the BNO08x 9-DOF Orientation IMU Fusion
 * Breakout
 *
 * 	This is a library for the Adafruit BNO08x breakout:
 * 	https://www.adafruit.com/product/4754
 *
 * 	Adafruit invests time and resources providing this open source code,
 *  please support Adafruit and open-source hardware by purchasing products from
 * 	Adafruit!
 *
 *  @section dependencies Dependencies
 *  This library depends on the Adafruit BusIO library
 *
 *  This library depends on the Adafruit Unified Sensor library
 *
 *  @section author Author
 *
 *  Bryan Siepert for Adafruit Industries
 *
 * 	@section license License
 *
 *  Software License Agreement (BSD License)
 * 
 *  Copyright (c) 2019 Bryan Siepert for Adafruit Industries
 *  All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *  1. Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *  notice, this list of conditions and the following disclaimer in the
 *  documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the copyright holders nor the
 *  names of its contributors may be used to endorse or promote products
 *  derived from this software without specific prior written permission.
 * 
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
 *  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 *  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * 	@section  HISTORY
 *
 *     v1.0 - First release
 */

#include "BNO085.h"

static int8_t _int_pin;

static sh2_SensorValue_t *_sensor_value = NULL;
static bool _reset_occurred = false;

static int i2chal_write(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len);
static int i2chal_read(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len,
                       uint32_t *t_us);
static void i2chal_close(sh2_Hal_t *self);
static int i2chal_open(sh2_Hal_t *self);

static uint32_t hal_getTimeUs(sh2_Hal_t *self);
static void hal_callback(void *cookie, sh2_AsyncEvent_t *pEvent);
static void sensorHandler(void *cookie, sh2_SensorEvent_t *pEvent);
static void hal_hardwareReset(void);

/**
 * @brief Construct a new BNO085::BNO085 object
 *
 */

/**
 * @brief Construct a new BNO085::BNO085 object
 *
 */
BNO085::BNO085(I2CBus *i2c, uint8_t address): _i2c(i2c), _address(address) {
}

/**
 * @brief Destroy the BNO085::BNO085 object
 *
 */
BNO085::~BNO085(void) {
}

/*!
 *    @brief  Sets up the hardware and initializes I2C
 *    @param  sensor_id
 *            The unique ID to differentiate the sensors from others
 *    @return True if initialization was successful, otherwise false.
 */
bool BNO085::init(int32_t sensor_id) {

  _HAL.open = i2chal_open;
  _HAL.close = i2chal_close;
  _HAL.read = i2chal_read;
  _HAL.write = i2chal_write;
  _HAL.getTimeUs = hal_getTimeUs;

  return _init_sensor(sensor_id);
}

/*!  @brief Initializer for post i2c/spi init
 *   @param sensor_id Optional unique ID for the sensor set
 *   @returns True if chip identified and initialized
 */
bool BNO085::_init_sensor(int32_t sensor_id) {
  int status;

  hardwareReset();

  // Open SH2 interface (also registers non-sensor event handler.)
  status = sh2_open(&_HAL, hal_callback, NULL);
  if (status != SH2_OK) {
    return false;
  }

  // Check connection partially by getting the product id's
  memset(&prodIds, 0, sizeof(prodIds));
  status = sh2_getProdIds(&prodIds);
  if (status != SH2_OK) {
    return false;
  }

  // Register sensor listener
  sh2_setSensorCallback(sensorHandler, NULL);

  return true;
}

/**
 * @brief Returns the yaw/pitch/roll data from the IMU
 *
 */
void BNO085::get_data(float *yaw, float *pitch, float *roll) {
    log_warn("IMPLEMENT GET DATA!");
}

/**
 * @brief Reset the device using the Reset pin
 *
 */
void BNO085::hardwareReset(void) { hal_hardwareReset(); }

/**
 * @brief Check if a reset has occured
 *
 * @return true: a reset has occured false: no reset has occoured
 */
bool BNO085::wasReset(void) {
  bool x = _reset_occurred;
  _reset_occurred = false;

  return x;
}

/**
 * @brief Fill the given sensor value object with a new report
 *
 * @param value Pointer to an sh2_SensorValue_t struct to fil
 * @return true: The report object was filled with a new report
 * @return false: No new report available to fill
 */
bool BNO085::getSensorEvent(sh2_SensorValue_t *value) {
  _sensor_value = value;

  value->timestamp = 0;

  sh2_service();

  if (value->timestamp == 0 && value->sensorId != SH2_GYRO_INTEGRATED_RV) {
    // no new events
    return false;
  }

  return true;
}

/**
 * @brief Enable the given report type
 *
 * @param sensorId The report ID to enable
 * @param interval_us The update interval for reports to be generated, in
 * microseconds
 * @return true: success false: failure
 */
bool BNO085::enableReport(sh2_SensorId_t sensorId,
                                   uint32_t interval_us) {
  static sh2_SensorConfig_t config;

  // These sensor options are disabled or not used in most cases
  config.changeSensitivityEnabled = false;
  config.wakeupEnabled = false;
  config.changeSensitivityRelative = false;
  config.alwaysOnEnabled = false;
  config.changeSensitivity = 0;
  config.batchInterval_us = 0;
  config.sensorSpecific = 0;

  config.reportInterval_us = interval_us;
  int status = sh2_setSensorConfig(sensorId, &config);

  if (status != SH2_OK) {
    return false;
  }

  return true;
}

/**************************************** I2C interface
 * ***********************************************************/

static int i2chal_open(sh2_Hal_t *self) {
    /* TODO
  // Serial.println("I2C HAL open");
  uint8_t softreset_pkt[] = {5, 0, 1, 0, 1};
  bool success = false;
  for (uint8_t attempts = 0; attempts < 5; attempts++) {
    //if (i2c_dev->write(softreset_pkt, 5)) {
    if(_i2c->write(_address, softreset_pkt, 5)) {
      success = true;
      break;
    }
    delay(30);
  }
  if (!success)
    return -1;
  delay(300);
  */
  return 0;
}

static void i2chal_close(sh2_Hal_t *self) {
}

static int i2chal_read(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len,
                       uint32_t *t_us) {
/*
  // uint8_t *pBufferOrig = pBuffer;

  uint8_t header[4];
  if (!i2c_dev->read(header, 4)) {
    return 0;
  }

  // Determine amount to read
  uint16_t packet_size = (uint16_t)header[0] | (uint16_t)header[1] << 8;
  // Unset the "continue" bit
  packet_size &= ~0x8000;

  *
  Serial.print("Read SHTP header. ");
  Serial.print("Packet size: ");
  Serial.print(packet_size);
  Serial.print(" & buffer size: ");
  Serial.println(len);
  *

  size_t i2c_buffer_max = i2c_dev->maxBufferSize();

  if (packet_size > len) {
    // packet wouldn't fit in our buffer
    return 0;
  }
  // the number of non-header bytes to read
  uint16_t cargo_remaining = packet_size;
  uint8_t i2c_buffer[i2c_buffer_max];
  uint16_t read_size;
  uint16_t cargo_read_amount = 0;
  bool first_read = true;

  while (cargo_remaining > 0) {
    if (first_read) {
      read_size = min(i2c_buffer_max, (size_t)cargo_remaining);
    } else {
      read_size = min(i2c_buffer_max, (size_t)cargo_remaining + 4);
    }

    // Serial.print("Reading from I2C: "); Serial.println(read_size);
    // Serial.print("Remaining to read: "); Serial.println(cargo_remaining);

    if (!i2c_dev->read(i2c_buffer, read_size)) {
      return 0;
    }

    if (first_read) {
      // The first time we're saving the "original" header, so include it in the
      // cargo count
      cargo_read_amount = read_size;
      memcpy(pBuffer, i2c_buffer, cargo_read_amount);
      first_read = false;
    } else {
      // this is not the first read, so copy from 4 bytes after the beginning of
      // the i2c buffer to skip the header included with every new i2c read and
      // don't include the header in the amount of cargo read
      cargo_read_amount = read_size - 4;
      memcpy(pBuffer, i2c_buffer + 4, cargo_read_amount);
    }
    // advance our pointer by the amount of cargo read
    pBuffer += cargo_read_amount;
    // mark the cargo as received
    cargo_remaining -= cargo_read_amount;
  }

  *
  for (int i=0; i<packet_size; i++) {
    Serial.print(pBufferOrig[i], HEX);
    Serial.print(", ");
    if (i % 16 == 15) Serial.println();
  }
  Serial.println();
  *

  return packet_size;
*/
  return 0;
}

static int i2chal_write(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len) {

/*
  size_t i2c_buffer_max = i2c_dev->maxBufferSize();

  *
  Serial.print("I2C HAL write packet size: ");
  Serial.print(len);
  Serial.print(" & max buffer size: ");
  Serial.println(i2c_buffer_max);
  *

  uint16_t write_size = min(i2c_buffer_max, len);
  if (!i2c_dev->write(pBuffer, write_size)) {
    return 0;
  }

  return write_size;
*/
  return 0;
}

/**************************************** HAL interface
 * ***********************************************************/

static void hal_hardwareReset(void) {
  // Reset pin not implemented
}

static uint32_t hal_getTimeUs(sh2_Hal_t *self) {
  uint32_t t = millis() * 1000;
  // Serial.printf("I2C HAL get time: %d\n", t);
  return t;
}

static void hal_callback(void *cookie, sh2_AsyncEvent_t *pEvent) {
  // If we see a reset, set a flag so that sensors will be reconfigured.
  if (pEvent->eventId == SH2_RESET) {
    // Serial.println("Reset!");
    _reset_occurred = true;
  }
}

// Handle sensor events.
static void sensorHandler(void *cookie, sh2_SensorEvent_t *event) {
  int rc;

  // Serial.println("Got an event!");

  rc = sh2_decodeSensorEvent(_sensor_value, event);
  if (rc != SH2_OK) {
    Serial.println("BNO08x - Error decoding sensor event");
    _sensor_value->timestamp = 0;
    return;
  }
}
