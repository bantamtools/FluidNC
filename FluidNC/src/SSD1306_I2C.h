
#pragma once

#include <OLEDDisplay.h>
#include "Machine/I2CBus.h"
#include <algorithm>

using namespace Machine;

class SSD1306_I2C : public OLEDDisplay {
private:
    uint8_t _address;
    I2CBus* _i2c;
    int     _frequency;
    bool    _error = false;
    int     _num_retries;

public:
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
        const int x_offset = (128 - this->width()) / 2;
#ifdef OLEDDISPLAY_DOUBLE_BUFFER
        uint8_t minBoundY = UINT8_MAX;
        uint8_t maxBoundY = 0;

        uint8_t minBoundX = UINT8_MAX;
        uint8_t maxBoundX = 0;
        uint8_t x, y;

        // Calculate the Y bounding box of changes
        // and copy buffer[pos] to buffer_back[pos];
        for (y = 0; y < (this->height() / 8); y++) {
            for (x = 0; x < this->width(); x++) {
                uint16_t pos = x + y * this->width();
                if (buffer[pos] != buffer_back[pos]) {
                    minBoundY = std::min(minBoundY, y);
                    maxBoundY = std::max(maxBoundY, y);
                    minBoundX = std::min(minBoundX, x);
                    maxBoundX = std::max(maxBoundX, x);
                }
                buffer_back[pos] = buffer[pos];
            }
            yield();
        }

        // If the minBoundY wasn't updated
        // we can savely assume that buffer_back[pos] == buffer[pos]
        // holdes true for all values of pos

        if (minBoundY == UINT8_MAX)
            return;

        sendCommand(COLUMNADDR);
        sendCommand(x_offset + minBoundX);  // column start address (0 = reset)
        sendCommand(x_offset + maxBoundX);  // column end address (127 = reset)

        sendCommand(PAGEADDR);
        sendCommand(minBoundY);  // page start address
        sendCommand(maxBoundY);  // page end address

        for (y = minBoundY; y <= maxBoundY; y++) {
            uint8_t* start = &buffer[(minBoundX + y * this->width()) - 1];
            uint8_t  save  = *start;

            *start = 0x40;  // control
            _i2c->write(_address, start, (maxBoundX - minBoundX) + 1 + 1);
            *start = save;
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
        _i2c->write(_address, (char*)&buffer[-1], displayBufferSize + 1);
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
        if (_num_retries == 3) {
            log_error("OLED failed to respond");
            _error = true;
        }
    }
};
