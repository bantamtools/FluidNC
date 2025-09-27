
#pragma once

#include <OLEDDisplay.h>
#include "Machine/I2CBus.h"
#include <algorithm>
#include <cstring>  // For memcpy in safe buffer implementation

#define OLED_USE_SAFE_BUFFER  // Comment out this line to use original buffer[-1] method

using namespace Machine;

class SSD1306_I2C : public OLEDDisplay {
private:
    uint8_t _address;
    I2CBus* _i2c;
    long     _frequency;
    bool    _error = false;
    int     _num_retries;
    
    uint32_t _next_display_ms = 0;
    static constexpr uint32_t MIN_DISPLAY_INTERVAL_MS = 50;  // 20 Hz max

public:
    volatile bool _refresh_needed = false;
    SSD1306_I2C(uint8_t address, OLEDDISPLAY_GEOMETRY g, I2CBus* i2c, int frequency) :
        _address(address), _i2c(i2c), _frequency(frequency), _error(false) {
        setGeometry(g);
    }

    bool connect() {
#if 0
        if (_frequency != -1) {
            _i2c->frequency(_frequency);
        }
#endif
        return true;
    }

    void display(void) {
        if (_error) {
            return;
        }
        
        // Mark that display needs refresh - don't do actual I2C work here
        _refresh_needed = true;
    }
    
    void performDisplayUpdate(void) {
        if (_error) {
            return;
        }
        
        // Only proceed if refresh is actually needed
        if (!_refresh_needed) {
            return;
        }
        _refresh_needed = false;
        
        // Continuously reset display registers to prevent corruption
        // Sends one command group per refresh, cycling through all protective commands
        static uint8_t reset_step = 0;

        switch(reset_step) {
            case 0: 
                sendCommand(0x40);  // Display start line = 0 (prevents vertical offset)
                break;
            case 1: 
                sendCommand(0x20);  // Set memory addressing mode command
                sendCommand(0x00);  // Horizontal addressing mode (required for double buffering)
                break;
            case 2: 
                sendCommand(0x2E);  // Deactivate scroll (prevents scrolling artifacts)
                break;
            case 3: 
                sendCommand(0xA1);  // Segment remap = flipped (matches flipScreenVertically)
                break;
            case 4: 
                sendCommand(0xC8);  // COM scan direction = flipped (matches flipScreenVertically)
                break;
            case 5: 
                sendCommand(0xD3);  // Display offset command
                sendCommand(0x00);  // Display offset = 0 (prevents vertical shift)
                break;
            case 6: 
                sendCommand(0x21);  // Column address range command
                sendCommand(0x00);  // Start column = 0
                sendCommand(0x7F);  // End column = 127 (prevents horizontal clipping)
                break;
            case 7: 
                sendCommand(0x22);  // Page address range command
                sendCommand(0x00);  // Start page = 0
                sendCommand(0x07);  // End page = 7 (64px height, prevents vertical clipping)
                break;
        }

        if (++reset_step > 7) {
            reset_step = 0;  // Restart sequence after completing all steps
        }
        
        const int x_offset = (128 - this->width()) / 2;
#ifdef OLEDDISPLAY_DOUBLE_BUFFER
        uint8_t minBoundY = UINT8_MAX;
        uint8_t maxBoundY = 0;

        uint8_t minBoundX = UINT8_MAX;
        uint8_t maxBoundX = 0;
        uint8_t x, y;

        // Calculate the Y bounding box of changes
        for (y = 0; y < (this->height() / 8); y++) {
            for (x = 0; x < this->width(); x++) {
                uint16_t pos = x + y * this->width();
                if (buffer[pos] != buffer_back[pos]) {
                    minBoundY = std::min(minBoundY, y);
                    maxBoundY = std::max(maxBoundY, y);
                    minBoundX = std::min(minBoundX, x);
                    maxBoundX = std::max(maxBoundX, x);
                }
            }
            yield();
        }

        // If the minBoundY wasn't updated, no changes were detected
        // Skip transmission to avoid unnecessary I2C traffic

        if (minBoundY == UINT8_MAX)
            return;

        sendCommand(COLUMNADDR);
        sendCommand(x_offset + minBoundX);  // column start address (0 = reset)
        sendCommand(x_offset + maxBoundX);  // column end address (127 = reset)

        sendCommand(PAGEADDR);
        sendCommand(minBoundY);  // page start address
        sendCommand(maxBoundY);  // page end address

        for (y = minBoundY; y <= maxBoundY; y++) {
#ifdef OLED_USE_SAFE_BUFFER
            // SAFE METHOD: Use separate buffer for I2C transfer to avoid buffer[-1] access
            static uint8_t i2c_buffer[129]; // Max width (128) + 1 control byte
            
            uint16_t data_width = (maxBoundX - minBoundX) + 1;
            
            // Set control byte at beginning of transfer buffer
            i2c_buffer[0] = 0x40;
            
            // Copy display data after control byte
            uint16_t buffer_offset = minBoundX + y * this->width();
            memcpy(&i2c_buffer[1], &buffer[buffer_offset], data_width);
            
            // Send the complete packet
            int write_result = _i2c->write(_address, i2c_buffer, data_width + 1);
            
            // Only update buffer_back after successful I2C write to prevent corruption
            if (write_result >= 0) {
                // OPTIMIZED VERSION: Use 64-bit operations for better performance
                uint16_t page_start_pos = minBoundX + y * this->width();
                
                // Fast copy using 64-bit unions where possible
                union {
                    uint8_t  bytes[8];
                    uint64_t qword;
                } fast_copy;
                
                uint8_t* src_ptr = &buffer[page_start_pos];
                uint8_t* dst_ptr = &buffer_back[page_start_pos];
                
                // Copy 8 bytes at a time where possible
                uint16_t fast_copies = data_width / 8;
                uint64_t* src64 = (uint64_t*)src_ptr;
                uint64_t* dst64 = (uint64_t*)dst_ptr;
                
                for (uint16_t i = 0; i < fast_copies; i++) {
                    dst64[i] = src64[i];
                }
                
                // Handle remaining bytes
                uint16_t remainder_start = fast_copies * 8;
                for (uint16_t i = remainder_start; i < data_width; i++) {
                    dst_ptr[i] = src_ptr[i];
                }
                
                // SIMPLE/DIRECT VERSION (commented out for reference):
                // Just copy the transmitted region byte-by-byte:
                // for (uint16_t i = 0; i < data_width; i++) {
                //     buffer_back[page_start_pos + i] = buffer[page_start_pos + i];
                // }
                // Or even simpler using memcpy:
                // memcpy(&buffer_back[page_start_pos], &buffer[page_start_pos], data_width);
            }
#else
            // ORIGINAL METHOD: Direct buffer manipulation (has buffer[-1] bug when minBoundX=0)
            uint8_t* start = &buffer[(minBoundX + y * this->width()) - 1];
            uint8_t  save  = *start;

            *start = 0x40;  // control
            int write_result = _i2c->write(_address, start, (maxBoundX - minBoundX) + 1 + 1);
            *start = save;
            
            // Only update buffer_back after successful I2C write to prevent corruption
            if (write_result >= 0) {
                uint16_t data_width = (maxBoundX - minBoundX) + 1;
                uint16_t page_start_pos = minBoundX + y * this->width();
                
                // OPTIMIZED VERSION: Use 64-bit operations for better performance
                uint8_t* src_ptr = &buffer[page_start_pos];
                uint8_t* dst_ptr = &buffer_back[page_start_pos];
                
                // Copy 8 bytes at a time where possible
                uint16_t fast_copies = data_width / 8;
                uint64_t* src64 = (uint64_t*)src_ptr;
                uint64_t* dst64 = (uint64_t*)dst_ptr;
                
                for (uint16_t i = 0; i < fast_copies; i++) {
                    dst64[i] = src64[i];
                }
                
                // Handle remaining bytes
                uint16_t remainder_start = fast_copies * 8;
                for (uint16_t i = remainder_start; i < data_width; i++) {
                    dst_ptr[i] = src_ptr[i];
                }
                
                // SIMPLE/DIRECT VERSION (commented out for reference):
                // Just copy the transmitted region byte-by-byte:
                // for (uint16_t i = 0; i < data_width; i++) {
                //     buffer_back[page_start_pos + i] = buffer[page_start_pos + i];
                // }
                // Or even simpler using memcpy:
                // memcpy(&buffer_back[page_start_pos], &buffer[page_start_pos], data_width);
            }
#endif
        }
#else

        sendCommand(COLUMNADDR);
        sendCommand(x_offset);                        // column start address (0 = reset)
        sendCommand(x_offset + (this->width() - 1));  // column end address (127 = reset)

        sendCommand(PAGEADDR);
        sendCommand(0x0);  // page start address (0 = reset)

        if (geometry == GEOMETRY_128_64) {
            sendCommand(0x7);
        } else if (geometry == GEOMETRY_128_32) {
            sendCommand(0x3);
        }

        buffer[-1] = 0x40;  // control
        _i2c->write(_address, &buffer[-1], displayBufferSize + 1);
#endif
    }

private:
    int getBufferOffset(void) { return 0; }

    inline void sendCommand(uint8_t command) __attribute__((always_inline)) {
        if (_error) {
            return;
        }
        uint8_t _data[2];
        _data[0] = 0x80;  // control
        _data[1] = command;
        _num_retries = 0;
        while ((_i2c->write(_address, _data, sizeof(_data)) < 0) && (_num_retries < 3)) {
            log_debug("OLED is not responding, retrying...");
            _num_retries++;
            delay_ms(100);
        }
        if (_num_retries == 3) { // May want to initialize some sort of recovery if this condition is reached.
            log_error("OLED failed to respond");
            _error = true;
        }
    }
};
