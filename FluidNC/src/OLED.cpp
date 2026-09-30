#include "OLED.h"
#include "Logging.h"
#include "Machine/MachineConfig.h"
#include "WebUI/WifiConfig.h"  // wifi_config.Hostname()
#include "Protocol.h"          // protocol_send_event, feedHoldEvent, cycleStartEvent, pollingPaused
#include "System.h"    // For sys.parkingInProgress access
#include "InputFile.h"  // InputFile (post-homing file reopen)
#include "Error.h"      // Error (InputFile constructor throws)
#include "SDFiles/SDFileTable.h"
#include "SDFiles/SDBrowser.h"
#include "OledTextFit.h"
#include <cmath>       // ceilf, floorf
#include <cstdio>      // snprintf
#include <cstring>     // memcpy, memset
#include <string>      // std::string
#include <vector>      // std::vector — split_to_width / popup_msg
#include <mutex>       // std::unique_lock — arena lock across the SD render
#include "WebUI/WebServer.h"   // WebUI::Web_Server::getUploadBytesReceived(), getUploadTotalSize()
#include "xmodem.h"            // xmodem_bytes_received

// Popup content area: rows 16-63, the part clearContentAreaFast wipes.
static constexpr int kPopupContentY = 16;
static constexpr int kPopupContentH = 48;

// Static variables
static float* saved_axes = NULL;   // Saved dro values for refreshing display
static bool saved_isMpos = false;
static bool* saved_limits = NULL;

static volatile JogState jog_state;

static int encoder_scroll_count = 0;
static int jog_scroll_count     = 0;  // jog-mode counterpart of encoder_scroll_count

// Jog target accumulator — always in machine coordinates (MPos/G53)
static float jog_target[MAX_N_AXIS] = {0};
static bool  jog_target_initialized = false;
static bool  jog_target_dirty = false;
static uint32_t jog_last_tick_ms = 0;
static State jog_prev_sys_state = State::Idle;
static char  jog_active_axis = '\0';  // Track which axis is being jogged
// True while a jog the OLED issued is still executing — emitted, but the machine
// has not yet completed the Jog->Idle cycle. Blocks the idle resync from snapping
// jog_target to a not-yet-updated mpos while our own command is in flight.
static bool  jog_in_flight = false;

// Bantam Tools logo (XBM format)
static uint8_t bantam_logo_bits[] PROGMEM = {
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x03, 0x80, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x0B, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x3E, 0x00, 0x2F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF2, 0x03, 0x3E, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0xCE, 0x0F, 0x37, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x3E, 0x0F, 0x1F, 0xF8, 0xC0, 0x61, 0xD8, 0x3F, 
  0x87, 0x83, 0x83, 0x7F, 0x3C, 0x78, 0x0C, 0x7C, 0xFC, 0x8E, 0x3F, 0xF8, 
  0xC1, 0xE1, 0xD8, 0x3F, 0x87, 0x83, 0x83, 0x7F, 0x7E, 0xFC, 0x0C, 0xFE, 
  0xF0, 0x8F, 0x0F, 0x18, 0x63, 0xE3, 0x18, 0x86, 0x8D, 0xC7, 0x03, 0x0C, 
  0xE7, 0xCE, 0x0D, 0xC6, 0xCC, 0xCF, 0x1F, 0x18, 0x63, 0xE3, 0x19, 0x86, 
  0x8D, 0xC7, 0x03, 0x0C, 0xC3, 0x86, 0x0D, 0x06, 0x3C, 0xCF, 0x1F, 0x18, 
  0x63, 0xE3, 0x19, 0x86, 0x8D, 0xC7, 0x03, 0x0C, 0xC3, 0x86, 0x0D, 0x0E, 
  0xF8, 0xEE, 0x07, 0xF8, 0x21, 0x62, 0x1B, 0x86, 0x88, 0x6D, 0x03, 0x0C, 
  0xC3, 0x86, 0x0D, 0x3C, 0xF0, 0xFF, 0x0F, 0xF8, 0x31, 0x66, 0x1B, 0xC6, 
  0x98, 0x6D, 0x03, 0x0C, 0xC3, 0x86, 0x0D, 0x78, 0xC8, 0xFF, 0x0F, 0x18, 
  0xF3, 0x67, 0x1E, 0xC6, 0x9F, 0x6D, 0x03, 0x0C, 0xC3, 0x86, 0x0D, 0xE0, 
  0x38, 0xFF, 0x0F, 0x18, 0xF3, 0x67, 0x1E, 0xC6, 0x9F, 0x39, 0x03, 0x0C, 
  0xC3, 0x86, 0x0D, 0xC0, 0xF0, 0xFE, 0x07, 0x18, 0x33, 0x66, 0x1C, 0xC6, 
  0x98, 0x39, 0x03, 0x0C, 0xE7, 0xCE, 0x0D, 0xC6, 0xC0, 0xFF, 0x03, 0xF8, 
  0x1B, 0x6C, 0x1C, 0x66, 0xB0, 0x11, 0x03, 0x0C, 0x7E, 0xFC, 0xFC, 0xFE, 
  0x00, 0xFF, 0x01, 0xF8, 0x19, 0x6C, 0x18, 0x66, 0xB0, 0x11, 0x03, 0x0C, 
  0x3C, 0x78, 0xFC, 0x7C, 0x00, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6F, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x77, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4C, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0xDC, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, };

// Settings icon 24x24 (XBM format)
static uint8_t settings_icon_bits[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x3C, 0x00, 0x20, 0x3C, 0x04,
  0x70, 0x3C, 0x0E, 0xF8, 0xFF, 0x1F, 0xF0, 0xFF, 0x0F, 0xE0, 0xC3, 0x07,
  0xE0, 0x00, 0x07, 0xE0, 0x00, 0x07, 0x7C, 0x00, 0x3E, 0x7E, 0x00, 0x7E,
  0x7E, 0x00, 0x7E, 0x7C, 0x00, 0x3E, 0xE0, 0x00, 0x07, 0xE0, 0x00, 0x07,
  0xE0, 0xC3, 0x07, 0xF0, 0xFF, 0x0F, 0xF8, 0xFF, 0x1F, 0x70, 0x3C, 0x0E,
  0x20, 0x3C, 0x04, 0x00, 0x3C, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00,
  };

// Home icon 24x24 (XBM format)
static uint8_t home_icon_bits[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x7E, 0x00, 0x00, 0xE7, 0x00,
  0x80, 0xC3, 0x01, 0xC0, 0x81, 0x03, 0xE0, 0x00, 0x07, 0x70, 0x00, 0x0E,
  0x38, 0x00, 0x1C, 0x1C, 0x00, 0x38, 0x0C, 0x00, 0x30, 0x0C, 0x00, 0x30,
  0x0C, 0x00, 0x30, 0x0C, 0xFF, 0x30, 0x0C, 0xFF, 0x30, 0x0C, 0xC3, 0x30,
  0x0C, 0xC3, 0x30, 0x0C, 0xC3, 0x30, 0x0C, 0xC3, 0x30, 0x0C, 0xC3, 0x30,
  0x0C, 0xC3, 0x30, 0xFC, 0xC3, 0x3F, 0xFC, 0xC3, 0x3F, 0x00, 0x00, 0x00,
  };

// Right arrow icon 24x24 (XBM format)
static uint8_t right_icon_bits[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x38, 0x00, 0x00, 0x7C, 0x00, 
  0x00, 0xF8, 0x00, 0x00, 0xF0, 0x01, 0x00, 0xE0, 0x03, 0x00, 0xC0, 0x07, 
  0x00, 0x80, 0x0F, 0x00, 0x00, 0x1F, 0xFE, 0xFF, 0x3F, 0xFE, 0xFF, 0x7F, 
  0xFE, 0xFF, 0x7F, 0xFE, 0xFF, 0x3F, 0x00, 0x00, 0x1F, 0x00, 0x80, 0x0F, 
  0x00, 0xC0, 0x07, 0x00, 0xE0, 0x03, 0x00, 0xF0, 0x01, 0x00, 0xF8, 0x00, 
  0x00, 0x7C, 0x00, 0x00, 0x38, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 
  };

// Left arrow icon 24x24 (XBM format)
static uint8_t left_icon_bits[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x3E, 0x00, 
  0x00, 0x1F, 0x00, 0x80, 0x0F, 0x00, 0xC0, 0x07, 0x00, 0xE0, 0x03, 0x00, 
  0xF0, 0x01, 0x00, 0xF8, 0x00, 0x00, 0xFC, 0xFF, 0x7F, 0xFE, 0xFF, 0x7F, 
  0xFE, 0xFF, 0x7F, 0xFC, 0xFF, 0x7F, 0xF8, 0x00, 0x00, 0xF0, 0x01, 0x00, 
  0xE0, 0x03, 0x00, 0xC0, 0x07, 0x00, 0x80, 0x0F, 0x00, 0x00, 0x1F, 0x00, 
  0x00, 0x3E, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 
  };

// Folder icon 24x24 (XBM format)
static uint8_t folder_icon_bits[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xFC, 0x01, 0x00, 0xFE, 0x03, 0x00, 0x06, 0xFF, 0x0F, 0x06, 0xFE, 0x1F,
  0x06, 0x00, 0x00, 0x86, 0xFF, 0x3F, 0x86, 0xFF, 0x7F, 0xC6, 0x00, 0x60,
  0xC6, 0x00, 0x60, 0xC6, 0x00, 0x30, 0x66, 0x00, 0x30, 0x66, 0x00, 0x30,
  0x66, 0x00, 0x18, 0x36, 0x00, 0x18, 0x36, 0x00, 0x18, 0xFE, 0xFF, 0x0F,
  0xFC, 0xFF, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };

// Draw/Run/Plot (pencil and squiggle) icon 24x24 (XBM format)
static uint8_t draw_icon_bits[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x3E, 0x00, 0x00,
  0xFC, 0x00, 0x00, 0xF0, 0x01, 0x00, 0xC0, 0x03, 0x04, 0x80, 0x07, 0x0E,
  0x00, 0x07, 0x1B, 0x00, 0x87, 0x31, 0x80, 0xC3, 0x68, 0xC0, 0x63, 0x34,
  0xF0, 0x31, 0x1A, 0xF8, 0x18, 0x0D, 0x3C, 0x8C, 0x06, 0x1C, 0x46, 0x03,
  0x0E, 0xA3, 0x01, 0x0E, 0xD5, 0x00, 0x0E, 0x69, 0x00, 0x1C, 0x31, 0x00,
  0x3C, 0x1F, 0x00, 0x78, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00,
  };

// Locked icon (for EggBot motors on)
static uint8_t lock_icon_bits[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0xFF, 0x00, 0x80, 0xE7, 0x01, 
  0x80, 0xC3, 0x01, 0xC0, 0x81, 0x03, 0xC0, 0x81, 0x03, 0xC0, 0x81, 0x03, 
  0xC0, 0x81, 0x03, 0xF8, 0xFF, 0x1F, 0xFC, 0xFF, 0x3F, 0xFC, 0xFF, 0x3F, 
  0x1C, 0x00, 0x38, 0x1C, 0x00, 0x38, 0x1C, 0x00, 0x38, 0x1C, 0xFF, 0x38, 
  0x1C, 0xFF, 0x38, 0x1C, 0x00, 0x38, 0x1C, 0x00, 0x38, 0x1C, 0x00, 0x38, 
  0xFC, 0xFF, 0x3F, 0xFC, 0xFF, 0x3F, 0xFC, 0xFF, 0x3F, 0x00, 0x00, 0x00, 
  };

// Unlocked icon (for EggBot motors off)
static uint8_t unlock_icon_bits[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0xFF, 0x00, 0x80, 0xE7, 0x01, 
  0x80, 0xC3, 0x01, 0xC0, 0x81, 0x03, 0xC0, 0x01, 0x00, 0xC0, 0x01, 0x00, 
  0xC0, 0x01, 0x00, 0xF8, 0xFF, 0x1F, 0xFC, 0xFF, 0x3F, 0xFC, 0xFF, 0x3F, 
  0x1C, 0x00, 0x38, 0x1C, 0x00, 0x38, 0x1C, 0x00, 0x38, 0x1C, 0xFF, 0x38, 
  0x1C, 0xFF, 0x38, 0x1C, 0x00, 0x38, 0x1C, 0x00, 0x38, 0x1C, 0x00, 0x38, 
  0xFC, 0xFF, 0x3F, 0xFC, 0xFF, 0x3F, 0xFC, 0xFF, 0x3F, 0x00, 0x00, 0x00,
  };

// 24x24, 72 bytes, generated from draw_again_icon.png
static uint8_t draw_again_icon_bits[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x80, 0xC3, 0x01,
    0xE0, 0x00, 0x07, 0x30, 0x40, 0x0C, 0x10, 0xE0, 0x08, 0x18, 0xB0, 0x19,
    0x08, 0x18, 0x13, 0x0C, 0x8C, 0x36, 0x04, 0x46, 0x23, 0x04, 0xA3, 0x21,
    0x84, 0xD1, 0x20, 0xC4, 0x68, 0x20, 0x4C, 0x35, 0x30, 0x48, 0x1A, 0x10,
    0x58, 0x0C, 0x18, 0xD0, 0x07, 0x08, 0x30, 0x00, 0x0C, 0x00, 0x08, 0x07,
    0x00, 0xC4, 0x01, 0x00, 0x7E, 0x00, 0x00, 0x04, 0x00, 0x00, 0x08, 0x00
};


// UTF-8 → font-index lookup registered with the SSD1306 driver in place of
// DefaultFontTableLookup; the mapping itself is font_table_lookup.
//
// Called byte-by-byte during drawStringInternal. State persists across the
// bytes of one string; it is effectively reset at the start of each string
// because incomplete sequences at the end of the previous string either
// completed or drifted out of the 3-byte state window. Text measurement uses
// its own FontLookupState, never this one.
static char customFontTableLookup(const uint8_t ch) {
    static FontLookupState state;
    return font_table_lookup(state, ch);
}


// Get the jogging state
JogState OLED::get_jog_state(void) {
    return jog_state;
}

// Set the jogging state
void OLED::set_jog_state(JogState state) {
    if (state == JogState::Scrolling) {
        jog_target_initialized = false;
        jog_target_dirty = false;
        jog_active_axis = '\0';
        jog_in_flight = false;
        _jog_cmd_ready = false;
        memset(_jog_prev_val, 0, sizeof(_jog_prev_val));
        _jog_full_redraw_ms = 0;  // Force full redraw on first update
    }
    if (state == JogState::Idle) {
        // Exiting jog mode: clear pending command, let in-progress
        // move finish naturally (no jog cancel)
        _jog_cmd_ready = false;
        jog_target_dirty = false;
        jog_in_flight = false;
    }
    jog_state = state;
}

// Returns true if OLED is active and ready
bool OLED::is_active() {
    return _active;
}

void OLED::show(Layout& layout, const char* msg) {
    if (_width < layout._width_required) {
        return;
    }
    _oled->setTextAlignment(layout._align);
    _oled->setFont(layout._font);
    _oled->drawString(layout._x, layout._y, msg);
}

OLED::Layout OLED::stateLayout          = { 0, 0, 0, DejaVu_Sans_10, TEXT_ALIGN_LEFT };
OLED::Layout OLED::elapsedTimeLayout    = { 63, 0, 128, DejaVu_Sans_10, TEXT_ALIGN_CENTER };
OLED::Layout OLED::percentLayout128     = { 128, 0, 128, DejaVu_Sans_10, TEXT_ALIGN_RIGHT };
OLED::Layout OLED::percentLayout64      = { 64, 0, 64, DejaVu_Sans_10, TEXT_ALIGN_RIGHT };
OLED::Layout OLED::posLabelLayout       = { 128, 15, 128, DejaVu_Sans_10, TEXT_ALIGN_RIGHT };
OLED::Layout OLED::radioAddrLayout      = { 128, 0, 128, DejaVu_Sans_10, TEXT_ALIGN_RIGHT };
OLED::Layout OLED::connectWifiLayout    = { 63, 52, 128, DejaVu_Sans_10, TEXT_ALIGN_CENTER };
OLED::Layout OLED::bottomTextLayout     = { 0, 52, 0, DejaVu_Sans_10, TEXT_ALIGN_LEFT };
OLED::Layout OLED::bottomRightLayout    = { 128, 52, 128, DejaVu_Sans_10, TEXT_ALIGN_RIGHT };

void OLED::group(Configuration::HandlerBase& handler) {
    if (_immutable) {
        Machine::MachineConfig::addWarning("OLED config ignored (using immutable defaults)");
        return;
    }
    handler.item("i2c_num", _i2c_num);
    handler.item("i2c_address", _address);
    handler.item("width", _width);
    handler.item("height", _height);
    handler.item("radio_delay_ms", _radio_delay);
}

void OLED::afterParse() {
    if (!config->_i2c[_i2c_num]) {
        log_error("i2c" << _i2c_num << " section must be defined for OLED");
        _error = true;
        return;
    }
    switch (_width) {
        case 128:
            switch (_height) {
                case 64:
                    _geometry = GEOMETRY_128_64;
                    break;
                case 32:
                    _geometry = GEOMETRY_128_32;
                    break;
                default:
                    log_error("For OLED width 128, height must be 32 or 64");
                    _error = true;
                    break;
            }
            break;
        case 64:
            switch (_height) {
                case 48:
                    _geometry = GEOMETRY_64_48;
                    break;
                case 32:
                    _geometry = GEOMETRY_64_32;
                    break;
                default:
                    log_error("For OLED width 64, height must be 32 or 48");
                    _error = true;
                    break;
            }
            break;
        default:
            log_error("OLED width must be 64 or 128");
            _error = true;
    }
}

void OLED::init() {
    if (_error) {
        return;
    }

    // Lock out encoder scrolling until menu ready
    _enc_scroll_lockout = true;

    log_info("OLED I2C address:" << to_hex(_address) << " width: " << _width << " height: " << _height);
    _oled = new SSD1306_I2C(_address, _geometry, config->_i2c[_i2c_num], 800000); // 800khz is maximum supported speed over esp32s3 i2c.
    // _oled = new SSD1306_I2C(_address, _geometry, config->_i2c[_i2c_num], 100000); 

    _oled->init();

    if(_oled->isArduino()){
        log_info("OLED Driver compiled for Arduino");
    } else {
        log_info("OLED Driver not for Arduino");
    }
    if(_oled->isDoubleBuffer()){
        log_info("OLED IsDoubleBuffer");
    } else {
        log_info("OLED !IsDoubleBuffer");
    }

    _oled->flipScreenVertically();

    // Install custom UTF-8 lookup so source strings can use real
    // Unicode codepoints (◀, →, ✓, ˣ, 🛜) directly instead of the
    // old "\xC2\xNN" prefix trick. See customFontTableLookup above.
    _oled->setFontTableLookupFunction(customFontTableLookup);

    _oled->setTextAlignment(TEXT_ALIGN_LEFT);

    _oled->clear();

    // Bantam Logo
    _oled->drawXbm(0, 18, _width, 26, bantam_logo_bits);
    // Machine name, FW version...
    show_state_text(config->_name);
    char bantam_ver_str[LIST_NAME_MAX_STR] = {"Firmware: "};
    strncat(bantam_ver_str, git_info_short, LIST_NAME_MAX_STR - 11);
    show(bottomTextLayout, bantam_ver_str);

    _oled->display();

    // Force immediate display update since polling hasn't started yet
    // This ensures the logo is actually sent to the OLED hardware
    SSD1306_I2C* ssd1306 = static_cast<SSD1306_I2C*>(_oled);
    if (ssd1306) {
        ssd1306->performDisplayUpdate();
    }

    // Pre-render jog mode headers for fast swapping
    initJogHeaders();

    jog_state = JogState::Idle;

    delay_ms(1000);

    allChannels.registration(this);
    setReportInterval(250);

    _file_job_running = false;

    _active = true;
}

Channel* OLED::pollLine(char* line) {
    autoReport();

    int16_t enc_diff = config->_encoder->get_difference();
    encoder_update(enc_diff);

    // Jog command dispatch via channel pipeline
    if (jog_state == JogState::Scrolling && jog_target_dirty) {
        uint32_t now = millis();
        bool user_quiet = (now - jog_last_tick_ms >= 100);
        bool machine_just_idled = (jog_prev_sys_state == State::Jog
                                   && sys.state == State::Idle);
        bool machine_idle = (sys.state == State::Idle);

        if ((user_quiet && machine_idle) || machine_just_idled) {
            // Format jog command into _pending_jog_cmd
            char axis_char = jog_active_axis;
            int axis_index = -1;
            switch (axis_char) {
                case 'X': axis_index = X_AXIS; break;
                case 'Y': axis_index = Y_AXIS; break;
                case 'Z': axis_index = Z_AXIS; break;
                case 'A': axis_index = A_AXIS; break;
                case 'B': axis_index = B_AXIS; break;
                case 'C': axis_index = C_AXIS; break;
                default: break;
            }

            if (axis_index >= 0) {
                snprintf(_pending_jog_cmd, sizeof(_pending_jog_cmd),
                         "$J=G53 %c%.3f F%.0f",
                         axis_char, jog_target[axis_index], JOG_FEEDRATE);
                _jog_cmd_ready = true;
                jog_target_dirty = false;
                // In flight only when this command will actually move the machine.
                // A no-op (e.g. clamped at a limit) leaves it clear so the resync
                // stays available.
                jog_in_flight = fabsf(jog_target[axis_index] - get_mpos()[axis_index]) > 0.05f;
            }
        }
    }

    // Resync jog_target to current position when idle.
    // Catches position changes from external jog sources (serial, web UI).
    if (jog_state == JogState::Scrolling && !jog_target_dirty) {
        uint32_t now = millis();
        bool user_quiet = (now - jog_last_tick_ms >= 100);
        bool machine_idle = (sys.state == State::Idle);

        if (user_quiet && machine_idle && !jog_in_flight) {
            memcpy(jog_target, get_mpos(), sizeof(float) * MAX_N_AXIS);
        }
    }

    // A jog we issued has landed once the machine returns to Idle from Jog.
    if (jog_in_flight && jog_prev_sys_state == State::Jog && sys.state == State::Idle) {
        jog_in_flight = false;
    }

    // Track state transitions for idle detection
    jog_prev_sys_state = sys.state;

    // Return pending jog command through channel pipeline if ready
    if (line != nullptr && _jog_cmd_ready) {
        strncpy(line, _pending_jog_cmd, Channel::maxLine - 1);
        line[Channel::maxLine - 1] = '\0';
        _jog_cmd_ready = false;
        return this;
    }

    return nullptr;
}

// Updates the menu with encoder values
void OLED::encoder_update(int16_t enc_diff) {
    if (enc_diff == 0) return;
    if ((abs(enc_diff) != 1) || _enc_scroll_lockout || _download_mode || _popup) return;

    // During busy screen: ignore encoder input
    if (_busy_stage != BusyStage::Off) return;

    // Save off the encoder difference for menu scrolling
    _enc_diff = enc_diff;

    // Jog target accumulation — works regardless of sys.state
    if (jog_state == JogState::Scrolling) {

        // Extract axis from menu item
        char *axis_str = (strrchr(_menu->get_selected()->display_name, ' ') + 1);
        char axis_char = axis_str[0];

        // Detect axis change — re-seed target for the new axis
        if (axis_char != jog_active_axis) {
            jog_target_initialized = false;
            jog_active_axis = axis_char;
            jog_scroll_count = 0;
        }

        // Newer encoders send two transitions per detent; act on every other
        // one so a detent is one jog increment, matching the menu scroll.
        if (!config->_encoder->_old_scroll_behavior) {
            if (jog_scroll_count == 0) {
                jog_scroll_count++;
                return;
            }
            jog_scroll_count = 0;
        }

        // Initialize jog_target from current machine position on first tick
        if (!jog_target_initialized) {
            memcpy(jog_target, get_mpos(), sizeof(float) * MAX_N_AXIS);
            jog_target_initialized = true;
        }

        // Determine axis index and step size
        int axis_index = -1;
        float jog_step = 1.0;
        switch (axis_char) {
            case 'X': axis_index = X_AXIS; jog_step = JOG_X_STEP; break;
            case 'Y': axis_index = Y_AXIS; jog_step = JOG_Y_STEP; break;
            case 'Z': axis_index = Z_AXIS; jog_step = JOG_Z_STEP; break;
            case 'A': axis_index = A_AXIS; jog_step = JOG_A_STEP; break;
            case 'B': axis_index = B_AXIS; jog_step = JOG_B_STEP; break;
            case 'C': axis_index = C_AXIS; jog_step = JOG_C_STEP; break;
            default: break;
        }

        // Accumulate and clamp in MPos space
        if (axis_index >= 0 && axis_index < config->_axes->_numberAxis) {
            jog_target[axis_index] += (jog_step * (float)_enc_diff);
            // Clamp to machine limits, snapping to step-aligned values.
            // Without snap, clamping to e.g. -0.5 with a 1mm step breaks
            // alignment — subsequent increments land on 0.5, 1.5, etc.
            float min_pos = limitsMinPosition(axis_index);
            float max_pos = limitsMaxPosition(axis_index);
            if (jog_target[axis_index] < min_pos)
                jog_target[axis_index] = ceilf(min_pos / jog_step) * jog_step;
            if (jog_target[axis_index] > max_pos)
                jog_target[axis_index] = floorf(max_pos / jog_step) * jog_step;

            // Mark target as changed and record tick time
            jog_target_dirty = true;
            jog_last_tick_ms = millis();
        }

        return;  // Don't fall through to menu scrolling
    }

    // Non-jog menu scrolling (only when idle)
    if (sys.state != State::Idle) return;

    // Refresh the menu
    show_menu();
}

void OLED::show_state() {
    // Special fast path for jog mode
    if (_menu != nullptr && _menu->is_jogging_menu()) {
        bool currently_jogging = (_state == "Jog");
        
        // Always update header to match current state during 250ms refresh
        showJogHeaderFast(currently_jogging);  // Includes separator line
        // No separator line needed - it's in the cached headers
    } else {
        // Normal state display path
        clearHeaderWithSeparator();

        std::string display_text;
        if (_popup && _error) {
            display_text = "Error";  // Show "Error" header for error popups
        } else if (_menu->is_home_menu() && _state != "Home") {
            display_text = config->_name;  // Show machine name on home menu
        } else if (_state == "Idle" && _menu != nullptr) {
            // During buffer sync (e.g., G54/G59 coordinate system changes), state briefly
            // transitions to Idle even though file/download is still active. Show "Run" to
            // avoid confusing menu flash.
            if (_file_job_running || _download_mode) {
                display_text = "Run";
            } else {
                display_text = _menu->get_current_menu_title();
            }
        } else {
            display_text = _state;
        }

        show_state_text(display_text);
    }
}

void OLED::show_limits(bool probe, const bool* limits) {
    if (_width != 128) {
        return;
    }
    if (_filename.length() != 0) {
        return;
    }
    if (_state == "Alarm") {
        return;
    }
    for (uint8_t axis = X_AXIS; axis < 3; axis++) {
        draw_checkbox(80, 27 + (axis * 10), 7, 7, limits[axis]);
    }
}



void OLED::show_menu() {
    // log_info("OLED Show Menu");
    int16_t menu_width;
    int16_t menu_height;
    int menu_max_active_entries;

    // Don't show menu during Alarm, Run or Hold states
    if (_busy_stage != BusyStage::Off || _state == "Alarm" || _state == "Run" || _state == "Hold:0" || _state == "Hold:1" || _download_mode || _file_job_running || _popup) {
        return;
    }

    // some things other than show_all() call show_menu() directly, so make sure we handle the special cases.
    if (_menu->is_home_menu() || _menu->is_run_menu() || _menu->is_postrun_menu()) {
        render_icon_menu();
        return;
    }

    _oled->setTextAlignment(TEXT_ALIGN_LEFT);

    // Set up font and menu window
    _oled->setFont(DejaVu_Sans_10);
    menu_height = 12;
    (_menu->is_full_width()) ? menu_width = 128 : menu_width = 64;
    menu_max_active_entries = 4;

    // Row labels start one pixel in and end one pixel short of the menu edge,
    // so a highlighted row keeps a lit column on both sides of its text.
    const int16_t row_text_x     = 1;
    const int16_t row_right_edge = menu_width - 1;

    // Clear any highlighting left in menu area
    if (menu_width == _width) {
        clearContentAreaFast();
    } else {
        _oled->setColor(BLACK);
        _oled->fillRect(0, _header_height, menu_width, _height);
        _oled->setColor(WHITE);
    }

    // Hold the arena lock across the selection update AND the row draw so the SD
    // list cannot be rebuilt from the other core mid-render. Recursive mutex:
    // clear_popup()->refresh_display()->show_menu() can re-enter from a write path.
    // Released before the I2C flush below so display() does not run under the lock.
    std::unique_lock<std::recursive_mutex> arenaLock(_menu->sd_table().mutex());

    // Re-anchor the SD cursor to the current table before applying input or drawing,
    // so a list mutated from another task (upload/delete/rename/card events) doesn't
    // leave the highlight on the wrong file.
    if (_menu->sd_browse_active()) {
        _menu->sd_browser().reconcile(_menu->sd_table(), menu_max_active_entries);
    }

    // Update the menu selection if not jogging
    if (jog_state == JogState::Idle) {
        // for old encoders, scroll on every tick; for new ones, every other tick
        //  (newer encoders send two transitions per tick)
        if (config->_encoder->_old_scroll_behavior) {
            if (_menu->sd_browse_active()) {
                _menu->sd_browser().moveSelection(_menu->sd_table(), _enc_diff, menu_max_active_entries);
            } else {
                _menu->update_selection(menu_max_active_entries, _enc_diff);
            }
            _enc_diff = 0; // Reset to prevent multiple scrolls
        } else {
            if (encoder_scroll_count > 0) {
                if (_menu->sd_browse_active()) {
                    _menu->sd_browser().moveSelection(_menu->sd_table(), _enc_diff, menu_max_active_entries);
                } else {
                    _menu->update_selection(menu_max_active_entries, _enc_diff);
                }
                _enc_diff = 0; // Reset to prevent multiple scrolls
                encoder_scroll_count = 0;
            } else {
                encoder_scroll_count++;
            }
        }
    }

    if (_menu->sd_browse_active()) {
        const sdfiles::SDFileTable& t = _menu->sd_table();
        sdfiles::SDBrowser& b = _menu->sd_browser();
        uint16_t top  = b.scrollTop();
        uint16_t rows = b.rowCount(t);
        for (int i = 0; i < menu_max_active_entries; ++i) {
            uint16_t row = static_cast<uint16_t>(top + i);
            if (row >= rows) break;
            bool sel = (row == b.selected());
            (sel) ? _oled->setColor(WHITE) : _oled->setColor(BLACK);
            _oled->fillRect(0, _header_height + (menu_height * i) + 1, menu_width, menu_height);
            (sel) ? _oled->setColor(BLACK) : _oled->setColor(WHITE);

            char label[sdfiles::kCompletionPrefixLen + sdfiles::kMaxNameLen + 1];
            if (sdfiles::SDBrowser::isBackRow(row)) {
                std::snprintf(label, sizeof(label), "%s", BACK_LABEL);
            } else {
                sdfiles::EntryId id = b.entryAtRow(t, row);
                size_t off = 0;
                if (id != sdfiles::kInvalidEntry && t.isCompleted(id)) {
                    std::memcpy(label, sdfiles::kCompletionPrefix, sdfiles::kCompletionPrefixLen);
                    off = sdfiles::kCompletionPrefixLen;
                }
                if (id != sdfiles::kInvalidEntry) {
                    t.copyName(id, label + off, sizeof(label) - off);
                    // Mark directories with a trailing '/' to distinguish them from files.
                    if (t.isDir(id)) {
                        size_t len = std::strlen(label);
                        if (len + 1 < sizeof(label)) {
                            label[len]     = '/';
                            label[len + 1] = '\0';
                        }
                    }
                } else {
                    label[off] = '\0';
                }
            }
            truncated_draw_string(row_text_x, _header_height + (menu_height * i), label, DejaVu_Sans_10, row_right_edge);
        }
    } else {
    // Traverse the list and print out each menu entry name
    ListNodeType *entry = _menu->get_active_head(); // Start at the beginning of the active window
    int i = 0;
    while (entry && entry->display_name && i < menu_max_active_entries) {

        // Highlight selected entry
        (entry->selected) ? _oled->setColor(WHITE) : _oled->setColor(BLACK);
        _oled->fillRect(0, _header_height + (menu_height * i) + 1, menu_width, menu_height);
        (entry->selected) ? _oled->setColor(BLACK) : _oled->setColor(WHITE);

        // Write out the entry name, bolding updated ones
        truncated_draw_string(row_text_x, _header_height + (menu_height * i), entry->display_name, DejaVu_Sans_10, row_right_edge); //(entry->updated ? DejaVu_Sans_Bold_10 : DejaVu_Sans_10));

        // Advance the line and pointer
        entry = entry->next;
        i++;
    }
    }
    arenaLock.unlock();  // arena reads are done; keep the I2C flush out of the lock
    _oled->display();
    _oled->setColor(WHITE); // if last entry was highlighted this could've been left on black, which is unexpected
    _enc_scroll_lockout = false;  // Unlock scrolling to use menu (if needed)
}

// Authoritative transition handler for the file-job flag. On the true->false
// edge (issued from InputFile::~InputFile after motion drains), latch the
// final elapsed time into _saved_run_time so the postrun screen can display
// it. Other transitions are no-ops beyond updating the flag itself.
void OLED::set_file_job_running(bool running) {
    if (_file_job_running && !running) {
        commit_elapsed_time();
    }
    _file_job_running = running;
}

// Fold any live running segment into _prev_run_time, then snapshot the total
// into _saved_run_time (seconds). If the job ends during a Hold, the Hold
// handler has already zeroed _run_start_time and folded its segment, so the
// fold here is skipped and the snapshot reflects pre-hold elapsed time only.
void OLED::commit_elapsed_time() {
    if (_run_start_time != 0) {
        _prev_run_time += (millis() - _run_start_time);
        _run_start_time = 0;
    }
    _saved_run_time = _prev_run_time / 1000;
}

// This is where file running menu stuff is drawn from?
void OLED::show_file() {
    // log_info("OLED show_file() called, _state=" << _state << ", _file_job_running=" << _file_job_running << ", _download_mode=" << _download_mode);
    // log_info("OLED Show file");
    char time_str[10];
    int pct = int(_percent);

    // Exit if file/download not running or no filename. Elapsed-time state is
    // managed by the raw-state edge handlers and set_file_job_running(); this
    // function is a pure reader of _run_start_time / _prev_run_time.
    // _popup guard mirrors show_dro's pattern: skip drawing while a popup is
    // displayed so the popup buffer isn't overpainted by clearContentAreaFast
    // below. Without this, popups that fire while _file_job_running is true
    // (e.g. the firmware-update notification from GCode.cpp's version-check
    // parser) are wiped within milliseconds by the next status-report-driven
    // show_all() call.
    if (_popup || (!_file_job_running && !_download_mode) || (_filename.length() == 0)) {
        return;
    }

    // Clear anything left in file areas
    _oled->setColor(BLACK);
    _oled->fillRect(40, 0, 88, 11); // clear entire time-elapsed area (40 to 128) - specific area, keep fillRect
    _oled->setColor(WHITE);
    clearContentAreaFast();

    if (_width == 128) {
        if (wifiDisconnected()) {
            _oled->setTextAlignment(TEXT_ALIGN_RIGHT);
            _oled->setFont(DejaVu_Sans_10);
            _oled->drawString(124, 0, (std::to_string(pct) + '%').c_str());
        } else {
            show(percentLayout128, std::to_string(pct) + '%');
        }

        if (_download_mode) {
            // TODO(busy-screen): This display will be replaced by
            // showBusyDisplay() when RSS downloads migrate to BusyScreen.

            truncated_draw_string(0, _header_height, "Downloading:", DejaVu_Sans_10, _width);
            truncated_draw_string(0, _header_height + 12, _filename, DejaVu_Sans_10, _width);
            _oled->drawProgressBar(0, _header_height + 12 + 16, 120, 10, pct);          

        } else {

            // Calculate and display the elapsed time
            uint32_t elapsed_time = (millis() - _run_start_time + _prev_run_time) / 1000;
            if (_state == "Hold" || _state == "Decel") {
                elapsed_time = _prev_run_time / 1000;
            }
            //elapsed_time += 35995; // temp test
            snprintf(time_str, 10, "%02d:%02d:%02d", 
                (elapsed_time / 3600),          // hours
                ((elapsed_time % 3600) / 60),   // minutes
                ((elapsed_time % 3600) % 60));  // seconds

            show(elapsedTimeLayout, time_str);

            truncated_draw_string(0, _header_height, _filename, DejaVu_Sans_10, _width);

            _oled->drawProgressBar(0, _header_height + 12, 120, 10, pct);
        }
    } else {
        show(percentLayout64, std::to_string(pct) + '%');
    }

    // Clear immediate comment when not in Run/Hold/Decel states
    if (_state != "Run" && _state != "Hold" && _state != "Decel" && _comment_countdown > 0) {
        _comment_countdown = 0;  // Clear immediate display on state exit
    }

    // Display pause/resume message at bottom OR comments if present
    if (_comment_countdown > 0) {
        // Immediate display comment active (e.g., "Resuming...")
        wrapped_draw_string(40, _comment, DejaVu_Sans_10);
    } else if (!_saved_m0_comment.empty() &&
               gc_state.modal.program_flow == ProgramFlow::Paused &&
               sys.state == State::Hold &&
               sys.suspend.bit.holdComplete &&
               (!config->_parking->park_on_feedhold() || sys.suspend.bit.retractComplete)) {
        // M0 pause fully stopped with saved comment, and parking is complete (if enabled)
        if (!_m0_comment_logged) {
            log_info("M0 message displayed: " << _saved_m0_comment);
            _m0_comment_logged = true;
        }
        wrapped_draw_string(40, _saved_m0_comment, DejaVu_Sans_10);
    } else if (!_download_mode) {
        // Default messages — centered
        _oled->setTextAlignment(TEXT_ALIGN_CENTER);
        int cx = _width / 2;
        if (_state == "Run") {
            if (_pause_requested) {
                _oled->drawString(cx, 46, "Pause requested...");
            } else {
                _oled->drawString(cx, 46, "Click to PAUSE");
            }
        } else if (_state == "Decel") {  // Hold:1 - decelerating
            _oled->drawString(cx, 46, "Pausing, please wait...");
        } else if (_state == "Hold") {    // Hold:0 - fully stopped
            // Check for unparking state - show "Resuming..." message
            if (sys.suspend.bit.initiateRestore) {
                _oled->drawString(cx, 46, "Resuming...");
            } else if (!config->_parking->park_on_feedhold() || sys.suspend.bit.retractComplete) {
                // Only show resume message if parking is disabled OR parking is complete
                clearLowerContentFast();  // Clear Y=40 to Y=63 efficiently
                _oled->drawString(cx, 40, "Click to RESUME");
                _oled->drawString(cx, 52, "HOLD to CANCEL");
            } else if (sys.parkingInProgress) {
                // Show "Pausing" only when actively parking (not after unpark completes)
                _oled->drawString(cx, 46, "Pausing, please wait...");
            }
        }
        _oled->setTextAlignment(TEXT_ALIGN_LEFT);  // Restore default
    }

    // Add a method to cleanup all drawn stuff to clear corruption?
}

void OLED::show_dro(float* axes, bool isMpos, bool* limits) {
    // log_info("OLED Show dro");
    // Save off dro values in case we need to refresh display with current data
    saved_axes = axes;
    saved_isMpos = isMpos;
    saved_limits = limits;

    if (_state == "Alarm" || _state == "Hold:0" || _state == "Hold:1" || _menu->is_full_width() || _popup || _file_job_running) {
        return;
    }

    if(axes == NULL) {
        show_error("ERROR: null axes in show_dro");
        return;
    }

    if (_state == "Run" && _width == 128 && _filename.length()) {
        // wide displays will show a progress bar instead of DROs
        return;
    }

    auto n_axis = config->_axes->_numberAxis;
    char axisVal[20];

    // Clear any highlighting left in DRO area
    _oled->setColor(BLACK);
    _oled->fillRect(64, _header_height, 64, _height);
    _oled->setColor(WHITE);

    // show(posLabelLayout, isMpos ? "Position" : "Offset");
    show(posLabelLayout, "Position G53");

    // Define preferred axis order per machine type (same as jog menu)
    int axis_order[3];
    switch (config->getMachineType()) {
        case Machine::MachineType::EggBot:
            // EggBot: A (egg), B (pen), Z (lift)
            axis_order[0] = A_AXIS;
            axis_order[1] = B_AXIS; 
            axis_order[2] = Z_AXIS;
            break;
            
        // Add future machine types here:
        // case Machine::MachineType::FutureMachine:
        //     axis_order[0] = ?_AXIS;
        //     axis_order[1] = ?_AXIS;
        //     axis_order[2] = ?_AXIS;
        //     break;
            
        default:
            // Default for all other machines: X, Y, Z
            axis_order[0] = X_AXIS;
            axis_order[1] = Y_AXIS;
            axis_order[2] = Z_AXIS;
            break;
    }

    _oled->setFont(DejaVu_Sans_10);
    uint8_t oled_y_pos;
    int display_count = 0;
    
    // Display axes in preferred order (limit to 3)
    for (int i = 0; i < 3 && display_count < 3; i++) {
        int axis = axis_order[i];
        if (axis < n_axis && config->_axes->_axis[axis] && 
            config->_axes->_axis[axis]->_motors[0] && 
            config->_axes->_axis[axis]->_motors[0]->isReal()) {
            
            oled_y_pos = _header_height + 12 * (display_count + 1);

            std::string axis_msg(1, Machine::Axes::_names[axis]);
            if (_width == 128) {
                axis_msg += ":";
            } else {
                // For small displays there isn't room for separate limit boxes
                // so we put it after the label
                axis_msg += limits[axis] ? "L" : ":";
            }
            _oled->setTextAlignment(TEXT_ALIGN_LEFT);
            _oled->drawString(68 + 0, oled_y_pos, axis_msg.c_str());

            _oled->setTextAlignment(TEXT_ALIGN_RIGHT);
            // snprintf(axisVal, 20 - 1, "%.3f", axes[axis]);
            snprintf(axisVal, 20 - 1, "%.3f", isMpos ? axes[axis] : (axes[axis] + get_wco()[axis]));
            _oled->drawString((_width == 128) ? 68 + 60 : 68 + 63, oled_y_pos, axisVal);
            
            display_count++;
        }
    }
    _oled->display();
}

// Clear a rectangle in the framebuffer using direct byte operations.
// Operates on the in-memory buffer only (no I2C). Much faster than
// fillRect() which calls drawVerticalLine() per column.
// x0,y0 is top-left inclusive, x1,y1 is bottom-right inclusive.
void OLED::clearBufferRect(int x0, int y0, int x1, int y1) {
    int w = _oled->width();
    uint8_t* buf = _oled->buffer;
    int page_start = y0 / 8;
    int page_end = y1 / 8;

    for (int page = page_start; page <= page_end; page++) {
        int top = page * 8;
        int bot = top + 7;
        int clear_top = (y0 > top) ? y0 : top;
        int clear_bot = (y1 < bot) ? y1 : bot;
        // Build mask of bits to clear: bit N corresponds to pixel y = page*8 + N
        uint8_t mask = (0xFF << (clear_top - top)) & (0xFF >> (bot - clear_bot));
        uint8_t inv_mask = ~mask;
        uint8_t* row = buf + page * w + x0;
        for (int x = x0; x <= x1; x++) {
            *row++ &= inv_mask;
        }
    }
}

void OLED::drawX3(int16_t x, int16_t y) {
    // 3x3 X pattern:  X.X
    //                  .X.
    //                  X.X
    _oled->setPixel(x,     y);
    _oled->setPixel(x + 2, y);
    _oled->setPixel(x + 1, y + 1);
    _oled->setPixel(x,     y + 2);
    _oled->setPixel(x + 2, y + 2);
}

bool OLED::wifiDisconnected() {
#ifdef ENABLE_WIFI
    int mode = WebUI::wifi_mode->get();
    return (mode == WebUI::WiFiSTA || mode == WebUI::WiFiFallback)
        && !WebUI::WiFiConfig::sta_got_ip();
#else
    return false;
#endif
}

void OLED::drawWifiDisconnectX() {
    if (wifiDisconnected()) {
        drawX3(125, 0);
    }
}

void OLED::show_jog_position_full() {
    if (_popup || _file_job_running) return;

    auto n_axis = config->_axes->_numberAxis;
    char axisVal[20];

    // Clear DRO area using fast byte operations
    clearBufferRect(64, _header_height, _width - 1, _height - 1);

    show(posLabelLayout, "Position G53");

    int axis_order[3];
    switch (config->getMachineType()) {
        case Machine::MachineType::EggBot:
            axis_order[0] = A_AXIS;
            axis_order[1] = B_AXIS;
            axis_order[2] = Z_AXIS;
            break;
        default:
            axis_order[0] = X_AXIS;
            axis_order[1] = Y_AXIS;
            axis_order[2] = Z_AXIS;
            break;
    }

    _oled->setFont(DejaVu_Sans_10);
    int display_count = 0;

    for (int i = 0; i < 3 && display_count < 3; i++) {
        int axis = axis_order[i];
        if (axis < n_axis && config->_axes->_axis[axis] &&
            config->_axes->_axis[axis]->_motors[0] &&
            config->_axes->_axis[axis]->_motors[0]->isReal()) {

            // Align with menu entries: entry 0 is BACK_LABEL, entries 1-3 are Jog axes
            // Menu uses _header_height + menu_height(12) * entry_index
            uint8_t oled_y_pos = _header_height + 12 * (display_count + 1);

            std::string axis_msg(1, Machine::Axes::_names[axis]);
            axis_msg += ":";
            _oled->setTextAlignment(TEXT_ALIGN_LEFT);
            _oled->drawString(68, oled_y_pos, axis_msg.c_str());

            _oled->setTextAlignment(TEXT_ALIGN_RIGHT);
            float display_val = get_mpos()[axis];
            snprintf(axisVal, sizeof(axisVal) - 1, "%.3f", display_val);
            _oled->drawString((_width == 128) ? 128 : 131, oled_y_pos, axisVal);

            // Cache the rendered string
            strncpy(_jog_prev_val[display_count], axisVal, sizeof(_jog_prev_val[0]));

            display_count++;
        }
    }

    _jog_full_redraw_ms = millis();
}

void OLED::show_jog_position_update() {
    if (_popup || _file_job_running) return;

    auto n_axis = config->_axes->_numberAxis;
    char axisVal[20];

    int axis_order[3];
    switch (config->getMachineType()) {
        case Machine::MachineType::EggBot:
            axis_order[0] = A_AXIS;
            axis_order[1] = B_AXIS;
            axis_order[2] = Z_AXIS;
            break;
        default:
            axis_order[0] = X_AXIS;
            axis_order[1] = Y_AXIS;
            axis_order[2] = Z_AXIS;
            break;
    }

    _oled->setFont(DejaVu_Sans_10);
    int display_count = 0;

    for (int i = 0; i < 3 && display_count < 3; i++) {
        int axis = axis_order[i];
        if (axis < n_axis && config->_axes->_axis[axis] &&
            config->_axes->_axis[axis]->_motors[0] &&
            config->_axes->_axis[axis]->_motors[0]->isReal()) {

            float display_val = get_mpos()[axis];
            snprintf(axisVal, sizeof(axisVal) - 1, "%.3f", display_val);

            // Only redraw if the formatted string changed
            if (strcmp(axisVal, _jog_prev_val[display_count]) != 0) {
                // Align with menu entries (same formula as show_jog_position_full)
                uint8_t oled_y_pos = _header_height + 12 * (display_count + 1);

                // Clear just the number region (x=80 to end, 10px tall)
                clearBufferRect(80, oled_y_pos, _width - 1, oled_y_pos + 9);

                _oled->setTextAlignment(TEXT_ALIGN_RIGHT);
                _oled->drawString((_width == 128) ? 128 : 131, oled_y_pos, axisVal);

                strncpy(_jog_prev_val[display_count], axisVal, sizeof(_jog_prev_val[0]));
            }

            display_count++;
        }
    }
}

void OLED::show_radio_info() {
    if (((_state == "Run" || _download_mode) && _filename.length()) || _state == "Hold:0" || _state == "Hold:1" || _file_job_running) {
        return;
    }

    // Don't clear the radio area when showing custom menu titles in Idle state
    // This prevents truncation of custom menu titles
    if (_state == "Idle" && _menu != nullptr) {
        return;
    }

    // Don't clear the header area when in jog mode - preserve jog header
    if (_menu != nullptr && _menu->is_jogging_menu()) {
        return;
    }

    // Clear anything left in radio area (avoid state text area)
    _oled->setColor(BLACK);
    _oled->fillRect(70, 0, 58, 11);  // Clear right side only to avoid state text conflict
    _oled->setColor(WHITE);

    if (_width == 128) {
        if (_state == "Alarm") {
            // show_error("Press button to CLEAR");
        } else if (_state != "Run") {
            //show(radioAddrLayout, _radio_addr); // not showing IP everywhere for now, but still clear area
        }
    } else {
        if (_state == "Alarm") {
            // show_error("Press button to CLEAR");
        }
    }
}

void OLED::show_error(std::string msg) {
    // Route errors through the unified popup path at Critical priority: persistent
    // (never auto-clears) and not displaceable by lower-priority popups, so a
    // must-see error participates in the popup lifecycle (preemption, clear_popup)
    // rather than the prior stateless draw that the next render could paint over.
    popup_msg(msg, 0, /*preserve_header=*/true, PopupLevel::Critical);
}

void OLED::show_all(float *axes, bool isMpos, bool *limits) {
    //_oled->clear();
    // log_info("OLED Show all");

    // Save off dro values here; otherwise with new menu regime we had no axes saved when entering jog menu
    saved_axes = axes;
    saved_isMpos = isMpos;
    saved_limits = limits;

    // Busy screen active — don't draw normal UI.
    // Rendering handled by updateBusyScreen() in the protocol loop.
    if (_busy_stage != BusyStage::Off) {
        return;
    }

    if ( (_menu->is_home_menu() || _menu->is_run_menu() || _menu->is_postrun_menu())
        && !(_state == "Alarm" || _state == "Hold" || _state == "Decel" || _download_mode || _file_job_running || _popup) ) {
        // in an icon menu, and not in a state where we don't show a menu at all
        render_icon_menu();
    } else if (jog_state != JogState::Idle) {
        // Jog mode: minimal updates only
        bool moving = (_state == "Jog");
        showJogHeaderFast(moving);
        if (!moving) {
            drawWifiDisconnectX();
        }
        uint32_t now = millis();
        if (now - _jog_full_redraw_ms >= JOG_FULL_REDRAW_INTERVAL_MS) {
            show_menu();
            show_jog_position_full();
        } else {
            show_jog_position_update();
        }
        _oled->display();
    } else {
        // Live-update WiFi Status menu before rendering header+entries
#ifdef ENABLE_WIFI
        if (_menu->is_wifi_info_menu()) {
            bool connected = WebUI::WiFiConfig::sta_got_ip();
            int mode = (int)WiFi.getMode();
            if (connected != _wifi_status_last_connected || mode != _wifi_status_last_mode) {
                _wifi_status_last_connected = connected;
                _wifi_status_last_mode = mode;
                _menu->rebuild_wifi_status();
            }
        }
#endif
        show_state();
        show_file();
        show_menu();
        show_dro(axes, isMpos, limits);
        show_radio_info();
        drawWifiDisconnectX();
        _oled->display();
    }
}

void OLED::render_icon_menu() {
    // log_info("render icon menu");
    // Update the menu selection if not jogging
    if (jog_state == JogState::Idle) {
        // for old encoders, scroll on every tick; for new ones, every other tick
        //  (newer encoders send two transitions per tick)
        if (config->_encoder->_old_scroll_behavior) {
            _menu->update_selection(4, _enc_diff);
            _enc_diff = 0; // Reset to prevent multiple scrolls
        } else {
            if (encoder_scroll_count > 0) {
                _menu->update_selection(4, _enc_diff);
                _enc_diff = 0; // Reset to prevent multiple scrolls
                encoder_scroll_count = 0;
            } else {
                encoder_scroll_count++;
            }
        }
    }

    // Home and Run menus have 3 entries; traverse to see which is selected
    ListNodeType *entry = _menu->get_active_head(); // Start at the beginning of the active window
    int i = 1;
    int selected = 1;
    while (entry && entry->display_name && i < 4) {
        if (entry->selected) { selected = i; }
        entry = entry->next;
        i++;
    }

    if (_menu->is_home_menu()) {
        show_home_layout(selected);
    } else if (_menu->is_run_menu()) {
        show_run_layout(selected);
    } else if (_menu->is_postrun_menu()) {
        if (sys.state == State::Idle) {
            show_postrun_layout(selected);
        } else {
            // The file job has ended but the machine is still draining the final
            // queued moves. Communicate that instead of showing an unusable menu;
            // this re-renders each refresh and flips to the post-run menu when the
            // machine reaches Idle (protocol_do_cycle_stop calls refresh_display).
            // No blocking wait.
            show_state();
            clearContentAreaFast();
            _oled->setFont(DejaVu_Sans_10);
            _oled->setTextAlignment(TEXT_ALIGN_CENTER);
            const int cx                = _width / 2;
            const int line_h            = font_height(DejaVu_Sans_10);
            constexpr int content_y     = 16;
            constexpr int content_h     = 48;
            const int     y             = content_y + (content_h - 2 * line_h) / 2;
            _oled->drawString(cx, y, "Finishing");
            _oled->drawString(cx, y + line_h, "motion queue");
            _oled->setTextAlignment(TEXT_ALIGN_LEFT);
            _oled->display();
        }
    }
    _enc_scroll_lockout = false;  // Unlock scrolling to use menu (if needed)
}

void OLED::show_home_layout(int hightlight) {
//    log_info("show home layout");
    // clear entire screen and set text state
    clearScreenFast();
    _oled->setTextAlignment(TEXT_ALIGN_LEFT);
    _oled->setFont(DejaVu_Sans_10);
    // top text
    if (_state == "Home") {
        show_state_text("Homing...");
    } else {
        // Machine name as specified in config file
        show_state_text(config->_name);
    }
    // three icons, order of this menu is now FILES - HOME - SETTINGS
    if (hightlight == 1) {
        _oled->fillRect(8, 18, 28, 28);
        _oled->setColor(BLACK);
        _oled->drawXbm(10, 20, 24, 24, folder_icon_bits);
        _oled->setColor(WHITE);
    } else {
        _oled->drawXbm(10, 20, 24, 24, folder_icon_bits);
    }
    if (hightlight == 2) {
        _oled->fillRect(50, 18, 28, 28);
        _oled->setColor(BLACK);
        if (config->getMachineType() == Machine::MachineType::EggBot) { // motor lock/unlock icon for Eggbot
            if (config->_axes->motors_are_disabled()) {
                _oled->drawXbm(52, 20, 24, 24, unlock_icon_bits);
            } else {
                _oled->drawXbm(52, 20, 24, 24, lock_icon_bits);
            }
        } else { // home icon for everything else
            _oled->drawXbm(52, 20, 24, 24, home_icon_bits);
        }
        _oled->setColor(WHITE);
    } else {
        if (config->getMachineType() == Machine::MachineType::EggBot) { // motor lock icon for Eggbot
            if (config->_axes->motors_are_disabled()) {
                _oled->drawXbm(52, 20, 24, 24, unlock_icon_bits);
            } else {
                _oled->drawXbm(52, 20, 24, 24, lock_icon_bits);
            }
        } else { // home icon for everything else
            _oled->drawXbm(52, 20, 24, 24, home_icon_bits);
        }
    }
    if (hightlight == 3) {
        _oled->fillRect(92, 18, 28, 28);
        _oled->setColor(BLACK);
        _oled->drawXbm(94, 20, 24, 24, settings_icon_bits);
        _oled->setColor(WHITE);
    } else {
        _oled->drawXbm(94, 20, 24, 24, settings_icon_bits);
    }

    // bottom text
    // get selected menu text
    ListNodeType *entry = _menu->get_active_head();
    int i = 0;
    while (entry && entry->display_name && i < 4) {
        if (entry->selected) {
            // For EggBot, show the action you can take (Lock/Unlock) instead of Home
            if (config->getMachineType() == Machine::MachineType::EggBot &&
                strcmp(entry->display_name, "Home") == 0) {
                if (config->_axes->motors_are_disabled()) {
                    show(bottomTextLayout, "Lock motors");
                } else {
                    show(bottomTextLayout, "Unlock motors");
                }
            } else {
                show(bottomTextLayout, entry->display_name);
            }
        }
        entry = entry->next;
        i++;
    }
    // homed indication
    if (hightlight == 2) { // home icon selected
        if(config->getMachineType() != Machine::MachineType::EggBot) { // standard homed check for non-EggBot machines
            if(config->_axes->_homed) {
                show(bottomRightLayout, "(homed)");
            } else {
                show(bottomRightLayout, "(unhomed)");
            }
        }
    }

    drawWifiDisconnectX();
    _oled->display();
}

void OLED::show_run_layout(int hightlight) {  // run menu
    log_info("Show run layout");
    // clear entire screen and set text state
    clearScreenFast();
    _oled->setTextAlignment(TEXT_ALIGN_LEFT);
    _oled->setFont(DejaVu_Sans_10);
    // top text
    // get selected menu text
    ListNodeType *entry = _menu->get_active_head();
    int i = 0;
    while (entry && entry->display_name && i < 4) {
        if (entry->selected) {
            show_state_text(entry->display_name);
        }
        entry = entry->next;
        i++;
    }
    // three icons
    if (hightlight == 1) {
        _oled->fillRect(8, 18, 28, 28);
        _oled->setColor(BLACK);
        _oled->drawXbm(10, 20, 24, 24, left_icon_bits);
        _oled->setColor(WHITE);
    } else {
        _oled->drawXbm(10, 20, 24, 24, left_icon_bits);
    }
    if (hightlight == 2) {
        _oled->fillRect(50, 18, 28, 28);
        _oled->setColor(BLACK);
        _oled->drawXbm(52, 20, 24, 24, draw_icon_bits);
        _oled->setColor(WHITE);
    } else {
        _oled->drawXbm(52, 20, 24, 24, draw_icon_bits);
    }
    if (hightlight == 3) {
        _oled->fillRect(92, 18, 28, 28);
        _oled->setColor(BLACK);
        _oled->drawXbm(94, 20, 24, 24, folder_icon_bits);
        _oled->setColor(WHITE);
    } else {
        _oled->drawXbm(94, 20, 24, 24, folder_icon_bits);
    }

    // bottom text : most recent file name
    show(bottomTextLayout, _menu->get_recent_file_name());

    drawWifiDisconnectX();
    _oled->display();
}

void OLED::draw_postrun_icon(int x, bool selected, const uint8_t* bits) {
    // Draws a 24x24 icon at (x,20); when selected, renders it inverse-video
    // inside a 28x28 highlight box at (x-2,18).
    if (selected) {
        _oled->fillRect(x - 2, 18, 28, 28);
        _oled->setColor(BLACK);
        _oled->drawXbm(x, 20, 24, 24, bits);
        _oled->setColor(WHITE);
    } else {
        _oled->drawXbm(x, 20, 24, 24, bits);
    }
}

void OLED::show_postrun_layout(int highlight) {
    // _saved_run_time is latched by commit_elapsed_time(), invoked from the
    // set_file_job_running(true->false) transition before this menu becomes
    // routable. This function is a pure reader.
    Menu::PostrunState state = _menu->compute_postrun_state();

    clearScreenFast();
    _oled->setTextAlignment(TEXT_ALIGN_LEFT);
    _oled->setFont(DejaVu_Sans_10);

    // Find the highlighted entry's label by walking the active window.
    ListNodeType* entry = _menu->get_active_head();
    const char* selected_label = nullptr;
    int i = 0;
    while (entry && entry->display_name && i < 4) {
        if (entry->selected) { selected_label = entry->display_name; }
        entry = entry->next;
        i++;
    }
    bool back_selected = selected_label && strcmp(selected_label, BACK_LABEL) == 0;
    bool next_selected = selected_label &&
                         strcmp(selected_label, "Run Next (HOLD to skip)") == 0;
    bool run_again_selected = selected_label && strcmp(selected_label, "Run Again") == 0;

    // Top line: Back shows the run outcome WITH elapsed time (latched for success,
    // cancel, and file error). After a cancel, Run Again's label also carries the "Canceled" status,
    // so it stays visible when Run Again is the cancel-default highlight (no room for a
    // separate status line). Other entries show their own label.
    if (back_selected) {
        char run_time_msg[24];
        const char* time_label;
        if (_menu->get_last_file_error()) {
            time_label = "Elapsed time";
        } else if (_menu->get_last_file_succeeded()) {
            time_label = "Completed in:";
        } else {
            time_label = "Canceled at";
        }
        snprintf(run_time_msg, 24, "%s %02d:%02d:%02d",
            time_label,
            (_saved_run_time / 3600),
            ((_saved_run_time % 3600) / 60),
            ((_saved_run_time % 3600) % 60));
        show_state_text(run_time_msg);
    } else if (run_again_selected && !_menu->get_last_file_succeeded()) {
        show_state_text("Canceled. Restart file?");
    } else if (selected_label) {
        show_state_text(selected_label);
    }

    // One icon per available post-run action, matching the entries built by
    // rebuild_postrun_menu: Back (always), Run Next (when a next file exists),
    // Run Again (unless this was a file error). Positioned by count so a removed
    // action leaves no dead icon: 3-up, 2-up, or a single centered icon.
    const bool has_next  = state.show_run_next;
    const bool has_again = !_menu->get_last_file_error();
    const int  icon_count = 1 + (has_next ? 1 : 0) + (has_again ? 1 : 0);
    static const int icon_x[4][3] = {
        {0, 0, 0},     // 0 (unused)
        {52, 0, 0},    // 1: centered
        {20, 84, 0},   // 2
        {10, 52, 94},  // 3
    };
    int slot = 0;
    draw_postrun_icon(icon_x[icon_count][slot], highlight == slot + 1, left_icon_bits);
    slot++;
    if (has_next) {
        draw_postrun_icon(icon_x[icon_count][slot], highlight == slot + 1, draw_icon_bits);
        slot++;
    }
    if (has_again) {
        draw_postrun_icon(icon_x[icon_count][slot], highlight == slot + 1, draw_again_icon_bits);
        slot++;
    }

    // Bottom line: Run Next -> next file; Back -> "All files complete!" when the folder is
    // done, else "Return to file list" (the filename serves no purpose on Back); Run Again
    // -> the just-run file (there it's useful — the file that would be restarted).
    if (next_selected) {
        show(bottomTextLayout, state.next_name.c_str());
    } else if (back_selected) {
        if (state.folder_complete) {
            // Centered celebratory message on the done screen.
            _oled->setFont(DejaVu_Sans_10);
            _oled->setTextAlignment(TEXT_ALIGN_CENTER);
            _oled->drawString(_width / 2, 52, "All files complete!");
            _oled->setTextAlignment(TEXT_ALIGN_LEFT);
        } else {
            show(bottomTextLayout, "Return to file list");
        }
    } else {
        show(bottomTextLayout, _menu->get_completed_file_name());
    }

    drawWifiDisconnectX();
    _oled->display();
}

void OLED::refresh_display(bool menu_only) {
    // showOLEDInfo();
    // Force reset the whole OLED buffer on refresh display.
    log_debug("Refresh Display");

    if (!_active) {
        log_info("Locked in refresh_display()");
        return;
    }

    // Don't overwrite the busy screen
    if (_busy_stage != BusyStage::Off) {
        return;
    }

    if (menu_only) {
        // : menu_only is "render only, don't consume input." Drop
        // any pending encoder rotation before show_menu() can apply it
        // as cursor motion. Action handlers that force a redraw via
        // refresh_display(true) — the in-place toggle / submenu-flip
        // primitives in Menu and Protocol — don't intend to consume
        // encoder events, but rotary buttons can emit spurious
        // rotation pulses when pressed (mechanical wobble). Without
        // this clear, a forced refresh inside an action handler
        // sometimes shifts the cursor one row, even though the user
        // only clicked. The regular periodic show_menu() tick remains
        // the canonical encoder-consumer.
        _enc_diff = 0;
        show_menu();
    } else if (jog_state != JogState::Idle) {
        show_menu();
        show_jog_position_full();
        _oled->display();
    } else {
        show_all(saved_axes, saved_isMpos, saved_limits);
    }
}

void OLED::processDisplayRefresh() {
    if (!_oled || !_active) {
        return;
    }

    // Auto-clear popups whose cooperative deadline has passed.
    // Replaces popup_msg's prior vTaskDelay-based auto-clear .
    // Signed-difference comparison handles millis() rollover.
    if (_popup_deadline_ms != 0 &&
        (int32_t)(millis() - _popup_deadline_ms) >= 0) {
        clear_popup();  // sets _popup_deadline_ms = 0 internally
    }

    // Redraw content when WiFi connection state changes
    static bool prev_wifi_disconnected = false;
    bool curr = wifiDisconnected();
    if (curr != prev_wifi_disconnected) {
        prev_wifi_disconnected = curr;
        refresh_display();
    }

    // Cast to SSD1306_I2C to access the refresh flag and method
    SSD1306_I2C* ssd1306 = static_cast<SSD1306_I2C*>(_oled);

    // Check timing and perform refresh if needed
    uint32_t now = millis();
    if (ssd1306->_refresh_needed && now >= _next_refresh_ms) {
        _next_refresh_ms = now + REFRESH_INTERVAL_MS;
        ssd1306->performDisplayUpdate();
    }
}

void OLED::popup_msg(const std::string& msg, int dly, bool preserve_header, PopupLevel level) {
    // Unified popup display . One function for all transient and
    // persistent popups; replaces the prior popup_msg + show_persistent_msg
    // pair. Centered horizontally, line block centered vertically.
    // Multi-line via embedded '\n'; long lines are word-wrapped to fit
    // display width.
    //
    // dly > 0 (default 2000): auto-clear after the interval via the
    //   cooperative deadline (_popup_deadline_ms) checked from
    //   processDisplayRefresh() each protocol-loop tick. Mechanism
    //   from  — avoids vTaskDelay's race window.
    // dly == 0: persistent. Clears only via clear_popup() or
    //   process_clear_command() (existing user-click path).
    //
    // preserve_header == true (default): clearContentAreaFast (rows
    //   16-63 only — header preserved). Vertical centering inside
    //   the content area. Avoids the visual disruption of headers
    //   blinking off and back, and sidesteps the show_state /
    //   show_radio_info partial-repaint issue (those header painters
    //   don't check _popup; if the header is wiped, their periodic
    //   repaint produces a partial-restore artifact).
    // preserve_header == false: clearScreenFast (full-screen wipe).
    //   Use when explicit visual emphasis is needed.
    //
    // level (default Normal): a lower-priority popup never displaces a higher-
    //   priority one already on screen; an equal level updates in place (e.g. the
    //   SD "Reading N files..." progress counter). Critical is forced persistent
    //   so a must-see message cannot silently auto-clear.
    if (_popup && level < _popup_level) {
        return;
    }
    if (level == PopupLevel::Critical) {
        dly = 0;
    }
    _popup       = true;
    _popup_level = level;
    ++_popup_seq;

    // Split on '\n' into raw lines (preserve empty lines).
    std::vector<std::string> raw;
    {
        size_t start = 0;
        for (size_t i = 0; i <= msg.size(); ++i) {
            if (i == msg.size() || msg[i] == '\n') {
                raw.emplace_back(msg.substr(start, i - start));
                start = i + 1;
            }
        }
    }

    // Word-wrap each raw line to display width, then cap at the number of
    // lines the content area holds; a longer message ends in "...".
    std::vector<std::string> lines;
    for (const auto& line : raw) {
        if (line.empty()) {
            lines.emplace_back();  // preserve blank lines
        } else {
            split_to_width(line, DejaVu_Sans_10, _width, lines);
        }
    }
    cap_lines(lines, popup_max_lines(), _width,
              [this](uint8_t c) { return static_cast<int>(char_width(c, DejaVu_Sans_10)); });

    // Vertical centering. Anchor and floor depend on preserve_header.
    // For preserve_header == true, the content area (rows 16-63) matches
    // clearContentAreaFast's actual page-aligned wipe — NOT
    // _header_height (15). Row 15 is page 1 / bit 7 and is never touched
    // by clearContentAreaFast, so anchoring at row 16 ensures the popup
    // text top lands on a known-cleared row.
    const int line_h  = popup_line_height();
    const int block_h = (int)lines.size() * line_h;
    int       y;
    if (preserve_header) {
        y = kPopupContentY + (kPopupContentH - block_h) / 2;
        if (y < kPopupContentY) {
            y = kPopupContentY;
        }
    } else {
        // Full-screen wipe: center over the whole 64px panel so a short popup sits at
        // true vertical center instead of low in the content area. Never start above
        // row 16, though — show_state() (DRO-style screens) can repaint rows 0-15 and
        // clip text there. The only current preserve_header=false caller is the postrun
        // skip toast, where show_state does not run, so the clamp is purely defensive.
        y = (64 - block_h) / 2;
        if (y < 16) {
            y = 16;
        }
    }

    if (preserve_header) {
        clearContentAreaFast();
    } else {
        clearScreenFast();
    }
    // Center each line by measuring it and drawing at an explicit x. Other
    // tasks paint the screen too and change the shared text alignment, so a
    // centered drawString() can land left-aligned at the midpoint.
    SSD1306_I2C* ssd1306 = static_cast<SSD1306_I2C*>(_oled);
    const auto   glyph_w = [this](uint8_t c) { return static_cast<int>(char_width(c, DejaVu_Sans_10)); };
    const int    cx      = _width / 2;
    for (const auto& l : lines) {
        ssd1306->drawStringAt(cx - text_width(l, glyph_w) / 2, y, l, DejaVu_Sans_10);
        y += line_h;
    }
    _oled->display();

    if (dly > 0) {
        _popup_deadline_ms = millis() + (uint32_t)dly;
        if (_popup_deadline_ms == 0) {
            _popup_deadline_ms = 1;  // sentinel: 0 means "no deadline"
        }
    } else {
        _popup_deadline_ms = 0;  // persistent
    }
}

// manual clear for errors that stick around (like unexpected end of file)
void OLED::clear() {
    if (_oled) {
        // _oled->clear();  // Obsolete - replaced with faster clearScreenFast()
        clearScreenFast();  // Much faster - uses direct memset on buffer
        _last_state_width = 0;  // Reset text width tracking since we cleared everything
    }
}

void OLED::clear_popup() {
    log_info("OLED clear popup called");
    _popup_deadline_ms = 0;  // explicit clear supersedes pending auto-clear
    _popup = false;
    _popup_level = PopupLevel::Normal;
    _error = false;
    refresh_display();
}

void OLED::clear_popup_if(uint32_t seq) {
    if (_popup && _popup_seq == seq) {
        clear_popup();
    }
}

void OLED::clear_popup(PopupLevel max_level) {
    // Dismiss only if the current popup is at or below max_level, so the background
    // SD/network layer can drop its own status popup without wiping a foreground one.
    if (_popup && _popup_level <= max_level) {
        clear_popup();
    }
}

// --- BusyScreen implementation ---

void OLED::busyPing() {
    // Hot path: Stage2 steady state
    if (_busy_stage == BusyStage::Stage2) {
        _busy_cmd_count++;
        _busy_idle_deadline_ms = millis() + 3000;  // Keep alive
        return;
    }

    uint32_t now = millis();

    if (_busy_stage == BusyStage::Off) {
        if (_busy_cmd_count == 0) {
            _busy_start_ms = now;
        }
        _busy_cmd_count++;
        _busy_idle_deadline_ms = now + 1500;
        return;
    }

    // Stage1: record activity
    _busy_cmd_count++;
    _busy_idle_deadline_ms = now + 1500;
}

void OLED::setBusy(BusyReason reason) {
    // Flag-only: do NOT draw here — may be called from WebServer task.
    // The main loop's updateBusyScreen() will pick this up and draw.
    _busy_reason = reason;
    _busy_cmd_count = 1;
    uint32_t now = millis();
    _busy_stage_shown_ms = now;
    _busy_start_ms = now;
    _busy_idle_deadline_ms = now + 3000;
    _busy_next_redraw_ms = 0;  // Force immediate redraw on next updateBusyScreen()
    _busy_stage = BusyStage::Stage2;  // Set stage LAST (volatile, read by other task)
}

void OLED::clearBusy(BusyReason reason) {
    if (_busy_stage == BusyStage::Off) return;
    if (_busy_reason != reason) return;

    if (reason == BusyReason::FileUpload) {
        // Flag-only: use dedicated cleanup flag (not _busy_reason)
        // to avoid race with setBusy() which also writes _busy_reason.
        _busy_cleanup_pending = true;
        _busy_stage = BusyStage::Off;
    } else {
        // Streaming: use idle timeout for graceful exit
        uint32_t now = millis();
        uint32_t min_end = _busy_stage_shown_ms + 2000;
        _busy_idle_deadline_ms = (now >= min_end) ? now : min_end;
        _busy_cmd_count = 0;
    }
}

void OLED::evaluateBusyTransitions() {
    uint32_t now = millis();

    if (_busy_stage == BusyStage::Off) {
        // Enter Stage1 immediately on any external command
        if (_busy_cmd_count > 0) {
            _busy_stage = BusyStage::Stage1;
            _busy_reason = BusyReason::GcodeStreaming;
            _busy_stage_shown_ms = now;
            _busy_next_redraw_ms = 0;  // Force immediate redraw
        }
        return;
    }

    // Check for Stage1 → Stage2 promotion
    if (_busy_stage == BusyStage::Stage1) {
        uint32_t elapsed = now - _busy_start_ms;
        if (elapsed >= 5000 && _busy_cmd_count > 1) {
            _busy_stage = BusyStage::Stage2;
            _busy_stage_shown_ms = now;
        }
    }

    // File uploads don't use idle timeout — only clearBusy() ends them.
    // Don't exit while machine is still in Run — avoids garbled display
    // when show_all() renders with Run state before Idle report arrives.
    if (_busy_reason != BusyReason::FileUpload &&
        _state != "Run" &&
        now >= _busy_idle_deadline_ms &&
        now >= _busy_stage_shown_ms + 1000) {
        exitBusyScreen();
    }
}

void OLED::exitBusyScreen() {
    _busy_stage = BusyStage::Off;
    _busy_reason = BusyReason::None;
    _busy_cmd_count = 0;

    // Full display recovery
    clearScreenFast();
    _jog_full_redraw_ms = 0;
    memset(_jog_prev_val, 0, sizeof(_jog_prev_val));
    _last_state_width = 0;
    refresh_display();
}

void OLED::cleanupAfterBusy() {
    // One-shot: clearBusy() sets _busy_cleanup_pending as a signal.
    // Using a dedicated flag avoids race with setBusy() writing _busy_reason.
    if (!_busy_cleanup_pending) return;
    _busy_cleanup_pending = false;

    _busy_reason = BusyReason::None;

    // Reset display caches so first normal frame redraws fully
    clearScreenFast();
    _jog_full_redraw_ms = 0;
    memset(_jog_prev_val, 0, sizeof(_jog_prev_val));
    _last_state_width = 0;

    // Force immediate normal display refresh
    refresh_display();
}

void OLED::updateBusyScreen() {
    evaluateBusyTransitions();  // Always evaluate — handles Off→Stage1 transition
    if (_busy_stage == BusyStage::Off) return;

    // Throttle content redraw to ~1 Hz
    uint32_t now = millis();
    if (now < _busy_next_redraw_ms) return;
    _busy_next_redraw_ms = now + 1000;

    showBusyDisplay();
    _oled->display();

    // Push to I2C immediately — do not rely on polling loop
    // timing, which is unreliable for cross-task buffer access.
    processDisplayRefresh();
}

void OLED::showBusyDisplay() {
    _oled->setFont(DejaVu_Sans_10);
    _oled->setTextAlignment(TEXT_ALIGN_CENTER);
    int cx = _width / 2;

    if (_busy_reason == BusyReason::FileUpload) {
        // Content area only — header pixels preserved in buffer
        clearContentAreaFast();
        clearHeaderWithSeparator();
        show(stateLayout, config->_name.c_str());
        _oled->setTextAlignment(TEXT_ALIGN_CENTER);
        showBusyUpload();
    } else {
        // Overlay box drawn on top of existing screen content
        int ox = 10;   // overlay x
        int oy = 14;   // overlay y
        int ow = 108;  // overlay width
        int oh = 42;   // overlay height

        _oled->setColor(BLACK);
        _oled->fillRect(ox - 1, oy - 1, ow + 2, oh + 2);   // Same outer black size
        _oled->setColor(WHITE);
        _oled->drawRect(ox + 1, oy + 1, ow - 2, oh - 2);   // Outer white, 2px black padding
        _oled->drawRect(ox + 2, oy + 2, ow - 4, oh - 4);   // Inner white, 2px thick total

        _oled->setFont(DejaVu_Sans_10);
        _oled->setTextAlignment(TEXT_ALIGN_CENTER);
        if (_busy_stage == BusyStage::Stage2) {
            _oled->drawString(cx, oy + 8, "Under external");
            _oled->drawString(cx, oy + 21, "streaming control");
        } else {
            _oled->drawString(cx, oy + 8, "External command");
            _oled->drawString(cx, oy + 21, "in progress");
        }
    }
}

void OLED::showBusyUpload() {
    _oled->setFont(DejaVu_Sans_10);
    _oled->setTextAlignment(TEXT_ALIGN_CENTER);
    int cx = _width / 2;
    int y = _header_height + 2;

    // Source label — use pollingPaused to distinguish USB vs WiFi
    const char* source;
    size_t bytes;
    size_t total;
    if (pollingPaused) {
        source = "USB file upload";
        bytes = xmodem_bytes_received;
        total = 0;  // Unknown until ; File size: sentinel is implemented
    } else {
        source = "WiFi file upload";
        bytes = WebUI::Web_Server::getUploadBytesReceived();
        total = WebUI::Web_Server::getUploadTotalSize();
    }

    _oled->drawString(cx, y, source);

    // Format progress text
    char progress[24];
    size_t bytes_kb = bytes / 1024;
    if (bytes_kb == 0 && bytes > 0) bytes_kb = 1;  // Minimum display: 1 kB
    size_t total_kb = total / 1024;

    // Determine unit from the larger value
    size_t larger_kb = (total_kb > 0) ? total_kb : bytes_kb;

    if (larger_kb <= 9999) {
        // Integer kB — cast to unsigned long for portable snprintf on ESP32
        if (total_kb > 0) {
            snprintf(progress, sizeof(progress), "%lu / %lu kB",
                     (unsigned long)bytes_kb, (unsigned long)total_kb);
        } else {
            snprintf(progress, sizeof(progress), "%lu kB received",
                     (unsigned long)bytes_kb);
        }
    } else {
        // MB with variable precision based on magnitude
        float bytes_mb = bytes_kb / 1024.0f;
        float larger_mb = larger_kb / 1024.0f;

        // Pick decimal places from the magnitude of the larger value
        const char* fmt;
        if (larger_mb < 10.0f) {
            fmt = (total_kb > 0) ? "%.3f / %.3f MB" : "%.3f MB received";
        } else if (larger_mb < 100.0f) {
            fmt = (total_kb > 0) ? "%.2f / %.2f MB" : "%.2f MB received";
        } else {
            fmt = (total_kb > 0) ? "%.1f / %.1f MB" : "%.1f MB received";
        }

        if (total_kb > 0) {
            float total_mb = total_kb / 1024.0f;
            snprintf(progress, sizeof(progress), fmt, bytes_mb, total_mb);
        } else {
            snprintf(progress, sizeof(progress), fmt, bytes_mb);
        }
    }

    _oled->drawString(cx, y + 14, progress);
}

void OLED::parse_numbers(std::string s, float* nums, int maxnums) {
    size_t pos     = 0;
    size_t nextpos = -1;
    size_t i       = 0;
    do {
        if (i >= maxnums) {
            return;
        }
        nextpos   = s.find_first_of(",", pos);
        auto num  = s.substr(pos, nextpos - pos);
        nums[i++] = std::strtof(num.c_str(), nullptr);
        pos       = nextpos + 1;
    } while (nextpos != std::string::npos);
}

void OLED::parse_axes(std::string s, float* axes) {
    size_t pos     = 0;
    size_t nextpos = -1;
    size_t axis    = 0;
    do {
        nextpos  = s.find_first_of(",", pos);
        auto num = s.substr(pos, nextpos - pos);
        if (axis < MAX_N_AXIS) {
            axes[axis++] = std::strtof(num.c_str(), nullptr);
        }
        pos = nextpos + 1;
    } while (nextpos != std::string::npos);
}

void OLED::parse_status_report() {
    if (_report.back() == '>') {
        _report.pop_back();
    }
    // Now the string is a sequence of field|field|field
    size_t pos     = 0;
    auto   nextpos = _report.find_first_of("|", pos);

    // Save previous MAPPED state from last call
    // NOTE: old_state contains the mapped display name (e.g., "Decel" not "Hold:1")
    std::string old_state = _state;

    // Get raw state from status report
    std::string raw_state = _report.substr(pos + 1, nextpos - pos - 1);

    // Handle timer state transitions BEFORE mapping (using raw state values)
    // Only when running a file job
    if (_file_job_running) {

        // Entering Hold:0 (fully stopped) or Alarm - accumulate elapsed time and stop timer
        if ((raw_state == "Hold:0" || raw_state == "Alarm") &&
            (old_state != "Hold" && old_state != "Alarm")) {
            if (_run_start_time != 0) {
                _prev_run_time += (millis() - _run_start_time);
                _run_start_time = 0;  // Clear timer while stopped
            }
        }

        // Leaving Hold or Alarm - restart timer
        if ((old_state == "Hold" || old_state == "Alarm") &&
            (raw_state != "Hold:0" && raw_state != "Alarm")) {
            _run_start_time = millis();
        }

        // Defensive: Start timer on first Run state if not already started
        // Handles race condition where state="Run" arrives before "Run file opened" message
        if (raw_state == "Run" && _run_start_time == 0) {
            _run_start_time = millis();
        }
    }

    // Map internal state names to user-friendly display names for _state
    if (raw_state == "Hold:1") {
        _state = "Decel";  // Decelerating
    } else if (raw_state == "Hold:0") {
        _state = "Hold";   // Fully stopped
    } else {
        _state = raw_state;
    }
    bool probe              = false;
    bool limits[MAX_N_AXIS] = { false };
    
    // Reset pause_requested flag before parsing (assume false unless found)
    _pause_requested = false;

    static float axes[MAX_N_AXIS];
    bool  isMpos    = false;
    _filename       = "";

    // ... handle it
    while (nextpos != std::string::npos) {
        pos        = nextpos + 1;
        nextpos    = _report.find_first_of("|", pos);
        auto field = _report.substr(pos, nextpos - pos);
        // MPos:, WPos:, Bf:, Ln:, FS:, Pn:, WCO:, Ov:, A:, SD:, DL: (ISRs:, Heap:)
        auto colon = field.find_first_of(":");
        auto tag   = field.substr(0, colon);
        auto value = field.substr(colon + 1);
        if (tag == "MPos") {
            // x,y,z,...
            parse_axes(value, axes);
            isMpos = true;
            continue;
        }
        if (tag == "WPos") {
            // x,y,z...
            parse_axes(value, axes);
            isMpos = false;
            continue;
        }
        if (tag == "Bf") {
            // buf_avail,rx_avail
            continue;
        }
        if (tag == "Ln") {
            // n
            auto linenum = std::strtol(value.c_str(), nullptr, 10);
            continue;
        }
        if (tag == "FS") {
            // feedrate,spindle_speed
            float fs[2];
            parse_numbers(value, fs, 2);  // feed in [0], spindle in [1]
            continue;
        }
        if (tag == "Pn") {
            // PXxYy etc
            for (char const& c : value) {
                switch (c) {
                    case 'P':
                        probe = true;
                        break;
                    case 'X':
                        limits[X_AXIS] = true;
                        break;
                    case 'Y':
                        limits[Y_AXIS] = true;
                        break;
                    case 'Z':
                        limits[Z_AXIS] = true;
                        break;
                    case 'A':
                        limits[A_AXIS] = true;
                        break;
                    case 'B':
                        limits[B_AXIS] = true;
                        break;
                    case 'C':
                        limits[C_AXIS] = true;
                        break;
                    case 'Q':  // New: pause reQuested
                        _pause_requested = true;
                        break;
                }
                continue;
            }
        }
        if (tag == "WCO") {
            // x,y,z,...
            // We do not use the WCO values because the DROs show whichever
            // position is in the status report
            // float wcos[MAX_N_AXIS];
            // auto  wcos = parse_axes(value, wcos);
            continue;
        }
        if (tag == "Ov") {
            // feed_ovr,rapid_ovr,spindle_ovr
            float frs[3];
            parse_numbers(value, frs, 3);  // feed in [0], rapid in [1], spindle in [2]
            continue;
        }
        if (tag == "A") {
            // SCFM
            int  spindle = 0;
            bool flood   = false;
            bool mist    = false;
            for (char const& c : value) {
                switch (c) {
                    case 'S':
                        spindle = 1;
                        break;
                    case 'C':
                        spindle = 2;
                        break;
                    case 'F':
                        flood = true;
                        break;
                    case 'M':
                        mist = true;
                        break;
                }
            }
            continue;
        }
        if (tag == "SD" || tag == "DL") {
            auto commaPos = value.find_first_of(",");
            _percent      = std::strtof(value.substr(0, commaPos).c_str(), nullptr);
            _filename     = value.substr(commaPos + 1);

            // Trim to just the file name (no path)
            _filename     = _filename.substr(_filename.rfind('/') + 1);
            continue;
        }
    }
    
    show_all(axes, isMpos, limits);
}

void OLED::parse_gcode_report() {
    size_t pos     = 0;
    size_t nextpos = _report.find_first_of(":", pos);
    auto   name    = _report.substr(pos, nextpos - pos);
//    log_info("+++gcode report: " << _report);
  // only seem to get these irregularly on start/pause/resume, even then not always
  // usually just of the form [GC:G0 G54 G17 G21 G90 G94 M3 M9 T0 F0 S90]
    if (name != "[GC") {
        return;
    }
    pos = nextpos + 1;
    do {
        nextpos  = _report.find_first_of(" ", pos);
        auto tag = _report.substr(pos, nextpos - pos);
        // G80 G0 G1 G2 G3  G38.2 G38.3 G38.4 G38.5
        // G54 .. G59
        // G17 G18 G19
        // G20 G21
        // G90 G91
        // G94 G93
        // M0 M1 M2 M30
        // M3 M4 M5
        // M7 M8 M9
        // M56
        // Tn
        // Fn
        // Sn
        //        if (tag == "G0") {
        //            continue;
        //        }
        pos = nextpos + 1;
    } while (nextpos != std::string::npos);
}


void OLED::parse_gcode_comment_report() {
    parse_gcode_comment_report(_report);
}

void OLED::parse_gcode_comment_report(std::string report) {
    // _report is GCCMT: + comment string
    size_t pos     = 0;
    size_t nextpos = report.find_first_of(":", pos);
    pos = nextpos + 1; // past header
    nextpos  = report.find_last_of("]"); // GC reports end in a bracket
    _comment = report.substr(pos, nextpos - pos); // trim header and bracket
//    log_info("!!! OLED parsed GC comment: " << _comment);
    if (_comment.find("CLEAR") != std::string::npos) {
        _comment_countdown = 0; // used to clear toolchange display immediately
//        log_info("ccd zeroed by CLEAR commend");
    }
    if (_comment.find("Tool") != std::string::npos) {
        _comment_countdown = 42; // now used as a failsafe to clear diplay later in absence of CLEAR comment
//        log_info("ccd = " << _comment_countdown);
    }
    // This can now be called directly from GCode parsing (to avoid clogging channel log reports), and
    //  refreshing immediately during that loop may be causing new issues. Instead, the comment state will
    //  persist to the next refresh which will be triggered by pause/run updates.
//    refresh_display();
}

void OLED::parse_error_report() {
    // example "[MSG:ERR: 34 (Gcode arc radius error) in /sd/arcbughunt4.gcode at line 49]"
    size_t pos     = 0;
    size_t nextpos = _report.find_first_of(":", pos);
    pos = nextpos + 1; // past MSG
    nextpos  = _report.find_last_of("]"); // report end in a bracket
    _report = _report.substr(pos, nextpos - pos); // trim MSG and bracket
    // Skip past "ERR: " prefix
    if (_report.substr(0, 5) == "ERR: ") {
        _report = _report.substr(5); // trim "ERR: " prefix
    }
    // Shorten the file location so the report fits in the popup:
    // " in /sd/path/file.gcode at line" -> " in file.gcode at line",
    // or " in G-code file at line", or just " at line".
    _report = fit_error_report(_report, popup_max_lines(), _width,
                               [this](uint8_t c) { return static_cast<int>(char_width(c, DejaVu_Sans_10)); });
    _popup = true;
    _error = true; // Mark as error popup to prevent WiFi overwrite
    show_error(_report); // popup error report until next button click
}

// [MSG:INFO: Connecting to STA:SSID foo]
void OLED::parse_STA() {
    // Don't overwrite active popup
    if (_popup) {
        return;
    }

    size_t start = strlen("[MSG:INFO: Connecting to STA SSID:");
    _radio_info  = _report.substr(start, _report.size() - start - 1);

    auto fh = font_height(DejaVu_Sans_10);
//    show(connectWifiLayout, "Connecting to Wi-Fi..."); // conflicts with showing version on boot screen
    _oled->display();
}

// [MSG:INFO: Connected - IP is 192.168.68.134]
void OLED::parse_IP() {
    // Don't overwrite active popup
    if (_popup) {
        return;
    }

    size_t start = _report.rfind(" ") + 1;
    _radio_addr  = _report.substr(start, _report.size() - start - 1);

    _oled->clear();
    wrapped_draw_string(0, "Wi-Fi Info", DejaVu_Sans_10);
    _oled->fillRect(0, _header_height - 2, _width, 2);  // Thick line
    wrapped_draw_string(_header_height, "Network ID: " + _radio_info, DejaVu_Sans_10); //DejaVu_Sans_Bold_10);
    wrapped_draw_string(_header_height + 12, "IP Addr: " + _radio_addr, DejaVu_Sans_10);
    _oled->display();
    delay_ms(_radio_delay);
}

// [MSG:INFO: AP SSID foo IP 192.168.68.134 mask foo channel foo]
void OLED::parse_AP() {
    // Don't overwrite active popup
    if (_popup) {
        return;
    }

    size_t start    = strlen("[MSG:INFO: AP SSID ");
    size_t ssid_end = _report.rfind(" IP ");
    size_t ip_end   = _report.rfind(" mask ");
    size_t ip_start = ssid_end + strlen(" IP ");

    _radio_info = "AP: ";
    _radio_info += _report.substr(start, ssid_end - start);
    _radio_addr = _report.substr(ip_start, ip_end - ip_start);

    _oled->clear();
    wrapped_draw_string(0, _radio_info, DejaVu_Sans_10);
    _oled->fillRect(0, _header_height - 2, _width, 2);  // Thick line
    wrapped_draw_string(_header_height + 1, _radio_addr, DejaVu_Sans_10);
    _oled->display();
    delay_ms(_radio_delay);
}



void OLED::parse_BT() {
    size_t      start  = strlen("[MSG:INFO: BT Started with ");
    std::string btname = _report.substr(start, _report.size() - start - 1);
    _radio_info        = "BT: ";
    _radio_info += btname.c_str();

    _oled->clear();
    wrapped_draw_string(0, _radio_info, DejaVu_Sans_10);
    _oled->display();
    delay_ms(_radio_delay);
}

void OLED::parse_report() {
    if (_report.length() == 0) {
        return;
    }
    // clear displayed toolchange comment after a certain number of following reports
    if (_comment_countdown > 0) { _comment_countdown--; }
//    if (_comment_countdown == 0) { log_info("Comment cleared by comment countdown"); }
    if (_report.rfind("<", 0) == 0) {
        //log_info("+++ < report: " << _report);
        parse_status_report();
        return;
    }
    if (_report.rfind("[GCCMT:", 0) == 0) {
        //log_info("+++ GC comment: " << _report);
        parse_gcode_comment_report();
        return;
    }
    if (_report.rfind("[GC:", 0) == 0) {
        //log_info("+++ GC report: " << _report);
        parse_gcode_report();
        return;
    }
    if (_report.rfind("[MSG:ERR:", 0) == 0) {
        //log_info("+++ OLED saw MSG:ERR:");
        parse_error_report();
        return;
    }
    if (_report.rfind("[MSG:INFO: Connecting to STA SSID:", 0) == 0) {
        parse_STA();
        return;
    }
    if (_report.rfind("[MSG:INFO: Connected", 0) == 0) {
        parse_IP();
        return;
    }
    if (_report.rfind("[MSG:INFO: AP SSID ", 0) == 0) {
        parse_AP();
        return;
    }
    if (_report.rfind("[MSG:INFO: BT Started with ", 0) == 0) {
        parse_BT();
        return;
    }
    if (_report.rfind("[MSG:INFO: File download started]", 0) == 0) {
        // TODO(busy-screen): Migrate RSS download to BusyScreen system.
        // When the RSS feed is re-exposed, replace _download_mode with
        // setBusy(BusyReason::FileUpload) and update RSSReader.cpp to
        // call clearBusy() on completion. See 2026-04-10-busy-screen-design.md.
        _download_mode = true;
        return;
    }
    if (_report.rfind("[MSG:INFO: File download completed]", 0) == 0) {
        _download_mode = false;        
        refresh_display();  // Makes sure we clear the download display
        return;
    }
    if (_report.rfind("[MSG:INFO: Run file opened]", 0) == 0) {
        _file_job_running = true;
        _saved_run_time = 0;
        _run_start_time = 0;  // Ensure clean start for new file
        _prev_run_time = 0;   // Reset accumulated time for new file
        return;
    }
    if (_report.rfind("[MSG:INFO: Run file closed]", 0) == 0) { // Moved directly to the ~InputFile() to avoid reporting inconsistencies.
        return;
    }
}

// This is how the OLED driver receives channel data
size_t OLED::write(uint8_t data) {
    char c = data;
    if (c == '\r') {
        return 1;
    }
    if (c == '\n') {
        parse_report();
        _report = "";
        return 1;
    }
    _report += c;
    return 1;
}

uint8_t OLED::font_width(font_t font) {
    return ((uint8_t*)font)[0];
}
uint8_t OLED::font_height(font_t font) {
    return ((uint8_t*)font)[1];
}
struct glyph_t {
    uint8_t msb;
    uint8_t lsb;
    uint8_t size;
    uint8_t width;
};
struct xfont_t {
    uint8_t width;
    uint8_t height;
    uint8_t first;
    uint8_t nchars;
    glyph_t glyphs[];
};
size_t OLED::char_width(uint8_t c, font_t font) {
    xfont_t* xf    = (xfont_t*)font;
    int      index = c - xf->first;
    return (index < 0) ? 0 : xf->glyphs[index].width;
}

uint16_t OLED::calculate_text_width(const std::string& text, font_t font) {
    uint16_t total_width = 0;
    for (char c : text) {
        total_width += char_width(c, font);
    }
    return total_width;
}

void OLED::show_state_text(const std::string& text) {
    // Add calibration indicator if active
    std::string displayText = text;
    if (rcServoZCal &&
        (config->getMachineType() == Machine::MachineType::EggBot ||
         config->getMachineType() == Machine::MachineType::WaterColorBot)) {
        displayText += " *";
    }

    // Calculate and save the width of the text we're about to display
    _last_state_width = calculate_text_width(displayText, DejaVu_Sans_10);

    // Display the text
    show(stateLayout, displayText);
}

void OLED::clearScreenFast() {
    // Equivalent to: _oled->fillRect(0, 0, _width, _height);
    // but much faster - same as _oled->clear() but more explicit
    
    uint16_t bufferSize = _oled->width() * _oled->height() / 8;
    memset(_oled->buffer, 0, bufferSize);
}

void OLED::clearContentAreaFast() {
    // Equivalent to: _oled->fillRect(0, 16, _width, _height - 16);
    // Clears content area (pixel rows 16-63, pages 2-7)
    
    uint64_t* qword_ptr = reinterpret_cast<uint64_t*>(_oled->buffer + 256);
    
    // Unroll loop: clear 8 qwords per iteration (64 bytes)
    // 768 bytes = 12 iterations of 64 bytes each
    for (int i = 0; i < 12; i++) {
        *qword_ptr++ = 0;  *qword_ptr++ = 0;  *qword_ptr++ = 0;  *qword_ptr++ = 0;
        *qword_ptr++ = 0;  *qword_ptr++ = 0;  *qword_ptr++ = 0;  *qword_ptr++ = 0;
    }
}

void OLED::clearLowerContentFast() {
    // Equivalent to: _oled->fillRect(0, 40, 128, 24);
    // Clear Y=40 to Y=63 (pages 5-7, bytes 640-1023)
    // This clears the lower 24 pixels (3 pages × 128 bytes/page = 384 bytes)
    uint64_t* qword_ptr = reinterpret_cast<uint64_t*>(_oled->buffer + 640);
    
    // 384 bytes = 48 qwords = 6 iterations of 8 qwords
    // Unrolled loop for maximum performance
    for (int i = 0; i < 6; i++) {
        *qword_ptr++ = 0;  *qword_ptr++ = 0;  *qword_ptr++ = 0;  *qword_ptr++ = 0;
        *qword_ptr++ = 0;  *qword_ptr++ = 0;  *qword_ptr++ = 0;  *qword_ptr++ = 0;
    }
}

void OLED::initJogHeaders() {
    // Save current buffer state (first 2 pages)
    uint8_t tempBuffer[256];
    memcpy(tempBuffer, _oled->buffer, 256);
    
    // Generate blank header with separator template using readable functions
    _oled->setColor(BLACK);
    _oled->fillRect(0, 0, _width, _header_height);           // Clear header area to black
    _oled->setColor(WHITE);
    _oled->fillRect(0, _header_height - 2, _width, 2);       // Draw separator in white
    memcpy(blankHeaderWithSeparator, _oled->buffer, 256);
    
    // Render "Jog mode" off-screen
    clearHeaderWithSeparator();
    _oled->setFont(DejaVu_Sans_10);
    _oled->setTextAlignment(TEXT_ALIGN_LEFT);
    _oled->drawString(0, 0, String("Jog mode"));
    memcpy(jogModeHeader, _oled->buffer, 256);
    
    // Render "Jog mode         - moving -" off-screen
    clearHeaderWithSeparator();
    _oled->drawString(0, 0, String("Jog mode         - moving -"));
    memcpy(jogModeMovingHeader, _oled->buffer, 256);
    
    // Restore original buffer
    memcpy(_oled->buffer, tempBuffer, 256);
    
    headersInitialized = true;
}

void OLED::clearHeaderWithSeparator() {
    memcpy(_oled->buffer, blankHeaderWithSeparator, 256);
}

void OLED::showJogHeaderFast(bool moving) {
    // Equivalent to: memcpy(_oled->buffer, cached, 256) + memset(_oled->buffer_back, 0xFF, 256)
    // but ~8× faster using 64-bit union operations instead of byte-by-byte copying
    
    uint8_t* cached = moving ? jogModeMovingHeader : jogModeHeader;
    
    // Use 64-bit unions for fast memory operations
    union {
        uint8_t  bytes[8];
        uint64_t qword;
    } data;
    
    uint64_t* src64 = (uint64_t*)cached;
    uint64_t* dst64 = (uint64_t*)_oled->buffer;
    
    // Copy 256 bytes in 32 operations (vs 256)
    for (int i = 0; i < 32; i++) {  // 256 bytes / 8 = 32 iterations
        dst64[i] = src64[i];
    }
    
    // Mark as dirty for double buffering
    #ifdef OLEDDISPLAY_DOUBLE_BUFFER
    uint64_t* back64 = (uint64_t*)_oled->buffer_back;
    data.qword = 0xFFFFFFFFFFFFFFFFULL;  // 8 bytes of 0xFF
    
    // Set dirty pattern in 32 operations (vs 256)  
    for (int i = 0; i < 32; i++) {  // 256 bytes / 8 = 32 iterations
        back64[i] = data.qword;
    }
    #endif
}

// Word-wrap a string into substring lines that each fit within max_w pixels
// at the given font. Used by both popup_msg (centered draw) and
// wrapped_draw_string (left-aligned draw); the algorithm is wrap_to_width.
void OLED::split_to_width(const std::string& s, font_t font, int max_w, std::vector<std::string>& out) {
    wrap_to_width(s, max_w, [this, font](uint8_t c) { return static_cast<int>(char_width(c, font)); }, out);
}

int OLED::popup_line_height() {
    return font_height(DejaVu_Sans_10) - 1;
}

size_t OLED::popup_max_lines() {
    return kPopupContentH / popup_line_height();
}

void OLED::wrapped_draw_string(int16_t y, const std::string& s, font_t font, bool setFont) {
    if (setFont) {
        _oled->setFont(font);
        _oled->setTextAlignment(TEXT_ALIGN_LEFT);
    }
    if (y > _height) {
        // Wrapped past the bottom of the screen; nothing to draw.
        return;
    }

    // Use the shared wrap algorithm. Draw each line at x=0 (left-aligned),
    // advancing y by font_height - 1 per line (preserves the historical
    // 1-pixel inter-line overlap behavior).
    std::vector<std::string> lines;
    split_to_width(s, font, _width, lines);

    const int line_h = font_height(font) - 1;
    for (const auto& line : lines) {
        if (y > _height) {
            break;
        }
        _oled->drawString(0, y, line.c_str());
        y += line_h;
    }
}

void OLED::truncated_draw_string(int16_t x, int16_t y, const std::string& s, font_t font, int16_t right_edge) {
    _oled->setFont(font);
    _oled->setTextAlignment(TEXT_ALIGN_LEFT);

    // Every glyph in the OLED font ends with one blank column, so a run of
    // glyphs with total advance w drawn at x has its last lit pixel at
    // column x + w - 2. It stays left of right_edge while w <= fit_width.
    const int fit_width  = right_edge - x + 1;
    const int dots_width = 3 * static_cast<int>(char_width('.', font));

    // Measure through the same UTF-8 lookup drawString uses, so multi-byte
    // characters count as the single glyph they render as.
    FontLookupState lookup;
    int    width     = 0;
    bool   fits      = true;
    size_t cut       = 0;  // bytes in the longest prefix that leaves room for "..."
    int    cut_width = 0;
    for (size_t i = 0; i < s.length(); ++i) {
        char glyph = font_table_lookup(lookup, static_cast<uint8_t>(s[i]));
        if (glyph == 0) {
            continue;  // inside a multi-byte sequence, or a dropped byte
        }
        width += static_cast<int>(char_width(static_cast<uint8_t>(glyph), font));
        if (width > fit_width) {
            fits = false;
            break;
        }
        if (width <= fit_width - dots_width) {
            cut       = i + 1;
            cut_width = width;
        }
    }

    if (fits) {
        _oled->drawString(x, y, s.c_str());
        return;
    }

    // Keep the ellipsis attached to the last word rather than a trailing space.
    while (cut > 0 && s[cut - 1] == ' ') {
        --cut;
        cut_width -= static_cast<int>(char_width(' ', font));
    }
    _oled->drawString(x, y, s.substr(0, cut).c_str());
    _oled->drawString(x + cut_width, y, "...");
}

void OLED::draw_checkbox(int16_t x, int16_t y, int16_t width, int16_t height, bool checked) {
    if (checked) {
        _oled->fillRect(x, y, width, height);  // If log.0
    } else {
        _oled->drawRect(x, y, width, height);  // If log.1
    }
}

void OLED::set_comment(const char* text, bool is_m0) {
    if (!text) return;
    
    if (is_m0) {
        // Save for M0 display
        _saved_m0_comment = text;
        if (_saved_m0_comment.length() > 64) {
            _saved_m0_comment = _saved_m0_comment.substr(0, 61) + "...";
        }
        _m0_comment_logged = false;  // Reset logging flag for new message
    } else {
        // Handle empty string as clear command
        if (*text == '\0') {
            _comment.clear();
            _comment_countdown = 0;
        } else {
            // Set immediate display with countdown
            _comment = text;
            if (_comment.length() > 64) {
                _comment = _comment.substr(0, 61) + "...";
            }
            _comment_countdown = 42;  // ~10.5 seconds at 4Hz
        }
    }
}

void OLED::clear_m0_comment() {
    _saved_m0_comment.clear();
    _m0_comment_logged = false;  // Reset logging flag
}

void OLED::process_clear_command() {
    // Clear immediate comment if displayed
    if (_comment_countdown > 0) {
        _comment.clear();
        _comment_countdown = 0;
    }
    
    // Clear M0 comment if we're in M0 pause state
    if (gc_state.modal.program_flow == ProgramFlow::Paused && 
        sys.state == State::Hold) {
        _saved_m0_comment.clear();
        _m0_comment_logged = false;  // Reset logging flag
    }
}
