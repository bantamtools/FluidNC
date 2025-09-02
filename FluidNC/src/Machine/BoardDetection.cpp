// Copyright (c) 2025 - Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "BoardDetection.h"
#include "../Logging.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string>

namespace Machine {
    // Cache detection result to avoid repeated probing
    static BoardType cachedBoardType = BoardType::Hen;  // Default assumption until detection runs
    static bool detectionComplete = false;
    
    // Board-specific I2C configurations from generated constants
    static constexpr gpio_num_t ROOSTER_SDA_PIN = (gpio_num_t)CriticalPins::Rooster::I2C_SDA;
    static constexpr gpio_num_t ROOSTER_SCL_PIN = (gpio_num_t)CriticalPins::Rooster::I2C_SCL;
    static constexpr gpio_num_t HEN_SDA_PIN = (gpio_num_t)CriticalPins::Hen::I2C_SDA;
    static constexpr gpio_num_t HEN_SCL_PIN = (gpio_num_t)CriticalPins::Hen::I2C_SCL;
    
    // TCA6408 I2C address (only present on Rooster boards)
    static constexpr uint8_t TCA6408_I2C_ADDR = 0x20;
    
    // TCA6408 register addresses (from datasheet)
    static constexpr uint8_t TCA6408_INPUT_REG = 0x00;     // Input port register
    static constexpr uint8_t TCA6408_OUTPUT_REG = 0x01;    // Output port register
    static constexpr uint8_t TCA6408_POLARITY_REG = 0x02;  // Polarity inversion register
    static constexpr uint8_t TCA6408_CONFIG_REG = 0x03;    // Configuration register
    
    // Low-level I2C probe function using ESP-IDF directly
    static bool probeI2CForTCA6408(gpio_num_t sda_pin, gpio_num_t scl_pin) {
        const i2c_port_t port = I2C_NUM_0;
        
        // Reset pins to ensure they're not in use
        gpio_reset_pin(sda_pin);
        gpio_reset_pin(scl_pin);
        
        // Configure I2C in master mode with internal pull-ups
        i2c_config_t conf = {};
        conf.mode = I2C_MODE_MASTER;
        conf.sda_io_num = sda_pin;
        conf.scl_io_num = scl_pin;
        conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
        conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
        conf.master.clk_speed = 100000;  // 100kHz for reliable detection
        conf.clk_flags = 0;
        
        // Initialize I2C driver
        esp_err_t ret = i2c_param_config(port, &conf);
        if (ret != ESP_OK) {
            log_debug("Board detect: i2c_param_config failed: " << esp_err_to_name(ret));
            return false;
        }
        
        ret = i2c_driver_install(port, conf.mode, 0, 0, 0);
        if (ret != ESP_OK) {
            log_debug("Board detect: i2c_driver_install failed: " << esp_err_to_name(ret));
            return false;
        }
        
        // Set timeout for quick detection
        #ifdef CONFIG_IDF_TARGET_ESP32S3
            i2c_set_timeout(port, 0x1F);  // Short timeout for ESP32-S3
        #else
            i2c_set_timeout(port, 0xFFFFF);  // Default for ESP32
        #endif
        
        // Probe for TCA6408 by reading its input register
        bool deviceFound = false;
        uint8_t data = 0;
        
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        if (cmd != NULL) {
            // Standard I2C register read sequence
            i2c_master_start(cmd);
            // Write device address with write bit
            i2c_master_write_byte(cmd, (TCA6408_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
            // Write register address to read from
            i2c_master_write_byte(cmd, TCA6408_INPUT_REG, true);
            // Repeated start
            i2c_master_start(cmd);
            // Write device address with read bit
            i2c_master_write_byte(cmd, (TCA6408_I2C_ADDR << 1) | I2C_MASTER_READ, true);
            // Read one byte
            i2c_master_read_byte(cmd, &data, I2C_MASTER_NACK);
            // Stop condition
            i2c_master_stop(cmd);
            
            // Execute the command with 20ms timeout
            ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(20));
            i2c_cmd_link_delete(cmd);
            
            if (ret == ESP_OK) {
                deviceFound = true;
                log_debug("TCA6408 responded with input reg value: 0x" << int(data));
            } else {
                // log_debug("TCA6408 probe failed: " << esp_err_to_name(ret));
            }
        }
        
        // Clean up I2C driver
        i2c_driver_delete(port);
        
        // Reset pins to high-impedance state
        gpio_reset_pin(sda_pin);
        gpio_reset_pin(scl_pin);
        
        // Small delay to ensure pins are fully released
        vTaskDelay(pdMS_TO_TICKS(10));
        
        return deviceFound;
    }
    
    BoardType detectBoardType() {
        // Return cached result if already detected
        if (detectionComplete) {
            return cachedBoardType;
        }
        
        log_info("Starting hardware board detection...");
        
        // Try Rooster I2C configuration (only Rooster has TCA6408)
        log_debug("Probing for TCA6408 with Rooster pins (SDA=" 
                 << ROOSTER_SDA_PIN << ", SCL=" << ROOSTER_SCL_PIN << ")");
        
        if (probeI2CForTCA6408(ROOSTER_SDA_PIN, ROOSTER_SCL_PIN)) {
            log_info("Board detection: Found TCA6408 - Identified as ROOSTER board");
            cachedBoardType = BoardType::Rooster;
            detectionComplete = true;
            return cachedBoardType;
        }
        
        // TCA6408 not found with Rooster pins - must be Hen board
        // (since only Rooster has the TCA6408 I/O expander)
        // log_info("Board detection: No TCA6408 found - Identified as HEN board");
        cachedBoardType = BoardType::Hen;
        detectionComplete = true;
        return cachedBoardType;
    }
    
    void resetBoardDetection() {
        cachedBoardType = BoardType::Hen;  // Default assumption until detection runs
        detectionComplete = false;
        log_debug("Board detection cache cleared");
    }
    
    BoardType getDetectedBoardType() {
        return cachedBoardType;
    }
    
    const char* getBoardTypeName(BoardType type) {
        switch(type) {
            case BoardType::Rooster: return "Bantam Tools Serama Rooster";
            case BoardType::Hen:     return "Bantam Tools Serama Hen";
            default:                 return "Bantam Tools Serama Hen";  // Should never happen, but default to Hen
        }
    }
}
