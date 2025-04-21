#include "OLED.h"
#include "Logging.h"
#include "Machine/MachineConfig.h"
#include "WebUI/WifiConfig.h"  // wifi_config.Hostname()

// Static variables
static float* saved_axes = NULL;   // Saved dro values for refreshing display
static bool saved_isMpos = false;
static bool* saved_limits = NULL;

static volatile JogState jog_state;
static volatile bool jog_timer_active;

static int encoder_scroll_count = 0;

// Bantam Tools logo (XBM format)
static uint8_t bantam_logo_bits[] PROGMEM = {
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x03, 0x80, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0E, 0x80, 0x0F, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x3E, 0x00, 0x2F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0xFE, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x03, 0x3E, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0xEC, 0x0F, 0x37, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0xBC, 0x0F, 0x1F, 0x78, 0xC0, 0xE1, 0xD8, 0x3F, 
  0x87, 0xC3, 0x83, 0x3F, 0x1E, 0x3C, 0x0C, 0x3C, 0xFC, 0x8E, 0x1F, 0xF8, 
  0xC1, 0xE1, 0xD8, 0x3F, 0x87, 0xC7, 0x83, 0x3F, 0x3F, 0x7E, 0x0C, 0x7E, 
  0xF0, 0xCF, 0x0F, 0x98, 0xE3, 0xE1, 0x19, 0x86, 0x87, 0xC7, 0x03, 0x0E, 
  0x73, 0xE6, 0x0C, 0x66, 0xD8, 0xCF, 0x1F, 0x18, 0x63, 0xE3, 0x19, 0x86, 
  0x8D, 0x47, 0x03, 0x8E, 0x73, 0xC6, 0x0C, 0x06, 0x78, 0xEF, 0x1F, 0x98, 
  0x61, 0xE3, 0x1B, 0x86, 0x8D, 0x65, 0x03, 0x8E, 0x73, 0xC6, 0x0C, 0x0E, 
  0xF0, 0xEF, 0x07, 0xF8, 0x61, 0x63, 0x1B, 0x86, 0x8D, 0x6D, 0x03, 0x8E, 
  0x73, 0xC6, 0x0C, 0x3C, 0xE0, 0xFF, 0x0F, 0xF8, 0x71, 0x63, 0x1B, 0xC6, 
  0x8D, 0x6D, 0x03, 0x8E, 0x73, 0xC6, 0x0C, 0x78, 0x30, 0xFF, 0x0F, 0x18, 
  0xF3, 0x67, 0x1E, 0xC6, 0x9F, 0x3D, 0x03, 0x8E, 0x73, 0xC6, 0x0C, 0x60, 
  0xF0, 0xFE, 0x0F, 0x18, 0xF3, 0x67, 0x1E, 0xC6, 0x9F, 0x39, 0x03, 0x8E, 
  0x73, 0xC6, 0x0C, 0xE2, 0xE0, 0xFF, 0x07, 0x98, 0x33, 0x66, 0x1C, 0xC6, 
  0x98, 0x39, 0x03, 0x8E, 0x73, 0xE6, 0x0C, 0xE7, 0x80, 0xFF, 0x03, 0xF8, 
  0x39, 0x66, 0x1C, 0xC6, 0x98, 0x39, 0x03, 0x0E, 0x3F, 0x7E, 0xFC, 0x7E, 
  0x00, 0xFE, 0x01, 0xF8, 0x18, 0x6E, 0x18, 0x66, 0xB0, 0x19, 0x03, 0x0E, 
  0x1E, 0x3C, 0xFC, 0x3C, 0x00, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xEF, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x76, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6C, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0xCC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, };

// Settings icon 24x24 (XBM format)
static uint8_t settings_icon_bits[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x66, 0x00, 0x00, 0x42, 0x00, 
  0x30, 0xC3, 0x0C, 0xF8, 0x81, 0x1F, 0xDC, 0x00, 0x3B, 0x0C, 0x00, 0x30, 
  0x06, 0x18, 0x60, 0x0E, 0x7E, 0x70, 0x1C, 0x7E, 0x38, 0x10, 0xFF, 0x08, 
  0x10, 0xFF, 0x08, 0x1C, 0x7E, 0x38, 0x0E, 0x7E, 0x70, 0x06, 0x18, 0x60, 
  0x0C, 0x00, 0x30, 0xDC, 0x00, 0x3B, 0xF8, 0x81, 0x1F, 0x30, 0xC3, 0x0C, 
  0x00, 0x42, 0x00, 0x00, 0x66, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00, 
  };

// Home icon 24x24 (XBM format)
static uint8_t home_icon_bits[] PROGMEM = {
  0x00, 0x3C, 0x00, 0x00, 0x7E, 0x00, 0x00, 0xFF, 0x00, 0x80, 0xE7, 0x01, 
  0xC0, 0xC3, 0x03, 0xE0, 0x81, 0x07, 0xF0, 0x00, 0x0F, 0x78, 0x00, 0x1E, 
  0x3C, 0x00, 0x3C, 0x1C, 0x00, 0x38, 0x1C, 0x00, 0x38, 0x1C, 0x00, 0x38, 
  0x1C, 0x00, 0x38, 0x1C, 0xFF, 0x38, 0x1C, 0xFF, 0x38, 0x1C, 0xC3, 0x38, 
  0x1C, 0xC3, 0x38, 0x1C, 0xC3, 0x38, 0x1C, 0xC3, 0x38, 0x1C, 0xC3, 0x38, 
  0xFC, 0xC3, 0x3F, 0xFC, 0xC3, 0x3F, 0xFC, 0xC3, 0x3F, 0x00, 0x00, 0x00, 
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
  0x00, 0x00, 0x00, 0xF8, 0x07, 0x00, 0xFC, 0x0F, 0x00, 0x0C, 0xFC, 0x07, 
  0x0C, 0xF8, 0x0F, 0x0C, 0x00, 0x00, 0x8C, 0xFF, 0x7F, 0xCC, 0xFF, 0x7F, 
  0xCC, 0x00, 0x30, 0xCC, 0x00, 0x30, 0xCC, 0x00, 0x30, 0xEC, 0x00, 0x30, 
  0x6C, 0x00, 0x38, 0x6C, 0x00, 0x18, 0x6C, 0x00, 0x18, 0x7C, 0x00, 0x18, 
  0xFC, 0xFF, 0x1F, 0xF8, 0xFF, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  };

// Draw/Run/Plot (pencil and squiggle) icon 24x24 (XBM format)
static uint8_t draw_icon_bits[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x01, 0x00, 
  0xF0, 0x03, 0x00, 0xF0, 0x07, 0x08, 0x00, 0x0F, 0x1C, 0x00, 0x0E, 0x3E, 
  0x00, 0x0E, 0x73, 0x80, 0x8F, 0xE1, 0xE0, 0xC7, 0x70, 0xF0, 0x63, 0x38, 
  0xF8, 0x30, 0x1C, 0x3C, 0x18, 0x0E, 0x1C, 0x0C, 0x07, 0x1C, 0x86, 0x03, 
  0x3C, 0xC3, 0x01, 0x78, 0xE7, 0x00, 0x70, 0x7F, 0x00, 0x60, 0x3F, 0x00, 
  0x00, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
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


// Jogging timer callback
static void jog_timer_cb(void* arg)
{
    // Extract the axis from arguments
    char *axis = (char*)(arg);

    // Enter jogging mode
    jog_state = JogState::Jogging;

    // Construct and run jog command
    String jog_command;
    switch (axis[0]) {
        case 'X': jog_command = "$J=X" + String(saved_axes[X_AXIS], 3) + " F" + String(JOG_FEEDRATE, 3); break;
        case 'Y': jog_command = "$J=Y" + String(saved_axes[Y_AXIS], 3) + " F" + String(JOG_FEEDRATE, 3); break;
        case 'Z': jog_command = "$J=Z" + String(saved_axes[Z_AXIS], 3) + " F" + String(JOG_FEEDRATE, 3); break;
        default: break;
    }   
    gc_execute_line((char*)jog_command.c_str());

    // Go back to scrolling mode
    jog_state = JogState::Scrolling;

    // Clear flag
    jog_timer_active = false;
}

// Get the jogging state
JogState OLED::get_jog_state(void) {
    return jog_state;
}

// Set the jogging state
void OLED::set_jog_state(JogState state) {
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
OLED::Layout OLED::posLabelLayout       = { 110, 15, 128, DejaVu_Sans_10, TEXT_ALIGN_RIGHT };
OLED::Layout OLED::radioAddrLayout      = { 128, 0, 128, DejaVu_Sans_10, TEXT_ALIGN_RIGHT };
OLED::Layout OLED::connectWifiLayout    = { 63, 52, 128, DejaVu_Sans_10, TEXT_ALIGN_CENTER };
OLED::Layout OLED::bottomTextLayout     = { 0, 52, 0, DejaVu_Sans_10, TEXT_ALIGN_LEFT };
OLED::Layout OLED::bottomRightLayout    = { 128, 52, 128, DejaVu_Sans_10, TEXT_ALIGN_RIGHT };

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
    _oled->setTextAlignment(TEXT_ALIGN_LEFT);

    _oled->clear();

    // Bantam Logo
    _oled->drawXbm(0, 18, _width, 26, bantam_logo_bits);
    // Machine name, FW version...
    show(stateLayout, config->_name.c_str());
    char bantam_ver_str[LIST_NAME_MAX_STR] = {"Version: "};
    strncat(bantam_ver_str, git_info_short, LIST_NAME_MAX_STR - 10);
    show(bottomTextLayout, bantam_ver_str); // TODO which version info do we want here?

    _oled->display();

    jog_state = JogState::Idle;

    delay_ms(1000);

    allChannels.registration(this);
    setReportInterval(250);

    _file_job_running = false;
    _job_just_started = false;

    _active = true;
}

Channel* OLED::pollLine(char* line) {
    autoReport();
    encoder_update(config->_encoder->get_difference());    
    return nullptr;
}

// Updates the menu with encoder values
void OLED::encoder_update(int16_t enc_diff) {
    // Bail if 1) Not IDLE, 2) excessive scrolling, 3) locked out or 4) downloading a file or 5) popup displaying
    if ((sys.state != State::Idle) || (abs(enc_diff) != 1) || _enc_scroll_lockout || _download_mode || _popup) return;
   
    // Save off the encoder difference to update the menu
    _enc_diff = enc_diff;
    //log_info("Saved encoder diff: " << enc_diff);

    // System IDLE and scrolling to jog
    if ((sys.state == State::Idle) && (jog_state == JogState::Scrolling)) {

        // Extract axis from menu item
        char *axis = (strrchr(_menu->get_selected()->display_name, ' ') + 1);

        // Start timer if not active
        if (!jog_timer_active) {

            // Set flag
            jog_timer_active = true;

            // Set up and start timer
            const esp_timer_create_args_t jog_timer_args = {
                .callback = &jog_timer_cb,
                .arg = (void*)axis,
                .name = "jog_timer"
            };
            esp_timer_handle_t jog_timer;
            ESP_ERROR_CHECK(esp_timer_create(&jog_timer_args, &jog_timer));
            ESP_ERROR_CHECK(esp_timer_start_once(jog_timer, JOG_TIMER_MS * 1000));
        }

        // Set the selected axis to the increment value and clamp to extents
        switch (axis[0]) {
            case 'X': 
            
                saved_axes[X_AXIS] += (JOG_X_STEP * (float)_enc_diff); 
                if (saved_axes[X_AXIS] < limitsMinPosition(X_AXIS)) saved_axes[X_AXIS] = limitsMinPosition(X_AXIS);
                if (saved_axes[X_AXIS] > limitsMaxPosition(X_AXIS)) saved_axes[X_AXIS] = limitsMaxPosition(X_AXIS);
                break;
            
            case 'Y': 
            
                saved_axes[Y_AXIS] += (JOG_Y_STEP * (float)_enc_diff);
                if (saved_axes[Y_AXIS] < limitsMinPosition(Y_AXIS)) saved_axes[Y_AXIS] = limitsMinPosition(Y_AXIS);
                if (saved_axes[Y_AXIS] > limitsMaxPosition(Y_AXIS)) saved_axes[Y_AXIS] = limitsMaxPosition(Y_AXIS);
                break;

            case 'Z': 
            
                saved_axes[Z_AXIS] += (JOG_Z_STEP * (float)_enc_diff); 
                if (saved_axes[Z_AXIS] < limitsMinPosition(Z_AXIS)) saved_axes[Z_AXIS] = limitsMinPosition(Z_AXIS);
                if (saved_axes[Z_AXIS] > limitsMaxPosition(Z_AXIS)) saved_axes[Z_AXIS] = limitsMaxPosition(Z_AXIS);
                break;
            
            default: break;
        }

        // Update the dro with the jog axis value
        show_dro(saved_axes, saved_isMpos, saved_limits);
    }

    // Refresh the menu
    show_menu();
}

void OLED::show_state() {

    // Clear anything left in radio area
    _oled->setColor(BLACK);
    _oled->fillRect(0, 0, 40, _header_height);
    _oled->setColor(WHITE);

    show(stateLayout, _state);
    _oled->fillRect(0, _header_height - 2, _width, 2);  // Thick line
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

    // Fail-safe, show error and lock out controls
    if (config->_i2c[0]->_fail_safe && !_startupConfigWarning) { // Only show this once per boot.
        _startupConfigWarning = true;
        // SAVE CONFIG FROM SD HERE
        popup_msg("Failure on boot. Attempting recovery from config on SD", 4000);
        Flashing::update_config_from_sdcard(config->_recoveryConfig, false);
    } else {
        // Don't show menu during Alarm, Run or Hold states
        if (_state == "Alarm" || _state == "Run" || _state == "Hold:0" || _state == "Hold:1" || _download_mode || _file_job_running || _popup) {
            //log_info("Failed state check in show_menu()");
            if(!_startupConfigWarning){
                //log_info("Returning from failed state check due to no startup config warning");
                return;
            }
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

        // Clear any highlighting left in menu area
        _oled->setColor(BLACK);
        _oled->fillRect(0, _header_height, menu_width, _height);
        _oled->setColor(WHITE);

        // Update the menu selection if not jogging
        if (jog_state == JogState::Idle) {
            // for old encoders, scroll on every tick; for new ones, every other tick
            //  (newer encoders send two transitions per tick)
            if (config->_encoder->_old_scroll_behavior) {
                _menu->update_selection(menu_max_active_entries, _enc_diff);
                _enc_diff = 0; // Reset to prevent multiple scrolls
            } else {
                if (encoder_scroll_count > 0) {
                    _menu->update_selection(menu_max_active_entries, _enc_diff);
                    _enc_diff = 0; // Reset to prevent multiple scrolls
                    encoder_scroll_count = 0;
                } else {
                    encoder_scroll_count++;
                }
            }
        }

        // Traverse the list and print out each menu entry name
        ListNodeType *entry = _menu->get_active_head(); // Start at the beginning of the active window
        int i = 0;
        while (entry && entry->display_name && i < menu_max_active_entries) {

            // Highlight selected entry
            (entry->selected) ? _oled->setColor(WHITE) : _oled->setColor(BLACK);
            _oled->fillRect(0, _header_height + (menu_height * i) + 1, menu_width, menu_height);
            (entry->selected) ? _oled->setColor(BLACK) : _oled->setColor(WHITE);

            // Write out the entry name, bolding updated ones
            truncated_draw_string(_header_height + (menu_height * i), entry->display_name, DejaVu_Sans_10); //(entry->updated ? DejaVu_Sans_Bold_10 : DejaVu_Sans_10));

            // Advance the line and pointer
            entry = entry->next;
            i++;
        }
        _oled->display();
        _oled->setColor(WHITE); // if last entry was highlighted this could've been left on black, which is unexpected
        _enc_scroll_lockout = false;  // Unlock scrolling to use menu (if needed)
    }
}

// This is where file running menu stuff is drawn from?
void OLED::show_file() {
    // log_info("OLED Show file");
    char time_str[10];
    int pct = int(_percent);

    // Record the start time if at beginning and clear at end of run
    if (_state == "Run" && _run_start_time == 0) {
        _run_start_time = millis();
        _saved_run_time = 0;

    } else if ((_state == "Idle" && !_file_job_running && !_download_mode) || pct == 100) {
//        _prev_run_time += (millis() - _run_start_time);
//        _saved_run_time = _prev_run_time / 1000;
        _run_start_time = 0;
        _prev_run_time = 0;
        return;
    }
    
    // Save off previous run time during a pause
    if ((_state == "Hold:0" || _state == "Hold:1") && (_run_start_time != 0)) {
        if (!_job_just_started) {
            _prev_run_time += (millis() - _run_start_time);
            _run_start_time = 0;
            //return;  // go ahead and draw the file running interface, in case run started with a Hold
        } else {
            _run_start_time = 0;
            _prev_run_time = 0;
        }
    }

    // Exit if file/download not running, no filename or have one last SD report
    if ((!_file_job_running && !_download_mode) || /*(!_download_mode && _run_start_time == 0) ||*/ (_filename.length() == 0) || (_state != "Run" && pct == 100)) {
        return;
    }

    // Clear anything left in file areas
    _oled->setColor(BLACK);
    _oled->fillRect(40, 0, 87, 11); // updated to clear entire time-elapsed area
    _oled->fillRect(0, _header_height, _width, _height);
    _oled->setColor(WHITE);

    if (_width == 128) {
        show(percentLayout128, std::to_string(pct) + '%');

        if (_download_mode) {
            
            truncated_draw_string(_header_height, "Downloading:", DejaVu_Sans_10);
            truncated_draw_string(_header_height + 12, _filename, DejaVu_Sans_10);
            _oled->drawProgressBar(0, _header_height + 12 + 16, 120, 10, pct);          

        } else {

            // Calculate and display the elapsed time
            uint32_t elapsed_time = (millis() - _run_start_time + _prev_run_time) / 1000;
            if (_job_just_started) { // edge case handling Hold on start
                elapsed_time = 0;
                _job_just_started = false;
            } else if ((_state == "Hold:0" || _state == "Hold:1")) {
                elapsed_time = _prev_run_time / 1000;
            }
            //elapsed_time += 35995; // temp test
            snprintf(time_str, 10, "%02d:%02d:%02d", 
                (elapsed_time / 3600),          // hours
                ((elapsed_time % 3600) / 60),   // minutes
                ((elapsed_time % 3600) % 60));  // seconds

            show(elapsedTimeLayout, time_str);

            truncated_draw_string(_header_height, _filename, DejaVu_Sans_10);

            _oled->drawProgressBar(0, _header_height + 12, 120, 10, pct);
        }
    } else {
        show(percentLayout64, std::to_string(pct) + '%');
    }

    // Display pause/resume message at bottom OR toolchange comment if present
    if (_comment_countdown > 0) {
        wrapped_draw_string(40, _comment, DejaVu_Sans_10);
    } else if (!_download_mode) {
        _oled->drawString(0, 40, "Click to PAUSE/RESUME");
        _oled->drawString(0, 52, "Long Press to CANCEL");
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

    show(posLabelLayout, isMpos ? "M Pos" : "W Pos");

    _oled->setFont(DejaVu_Sans_10);
    uint8_t oled_y_pos;
    for (uint8_t axis = X_AXIS; axis < n_axis; axis++) {
        oled_y_pos = ((_height == 64) ? 26 : 19) + (axis * 10);

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
        snprintf(axisVal, 20 - 1, "%.3f", axes[axis]);
        _oled->drawString((_width == 128) ? 68 + 60 : 68 + 63, oled_y_pos, axisVal);
    }
    _oled->display();
}

void OLED::show_radio_info() {
    if (((_state == "Run" || _download_mode) && _filename.length()) || _state == "Hold:0" || _state == "Hold:1" || _file_job_running) {
        return;
    }

    // Clear anything left in radio area
    _oled->setColor(BLACK);
    _oled->fillRect(30, 0, 98, 11);
    _oled->setColor(WHITE);

    if (_width == 128) {
        if (_state == "Alarm" && !config->_i2c[0]->_fail_safe) {
            // show_error("Press button to CLEAR");
        } else if (_state != "Run") {
            //show(radioAddrLayout, _radio_addr); // not showing IP everywhere for now, but still clear area
        }
    } else {
        if (_state == "Alarm" && !config->_i2c[0]->_fail_safe) {
            // show_error("Press button to CLEAR");
        }
    }
}

void OLED::show_error(std::string msg) {

    // Clear anything left in error message area
    _oled->setColor(BLACK);
    _oled->fillRect(0, _header_height, _width, _height);
    _oled->setColor(WHITE);

    // Draw message
    //truncated_draw_string(_header_height, msg, DejaVu_Sans_10);
    wrapped_draw_string(_header_height, msg, DejaVu_Sans_10);
    _oled->display();    
}

void OLED::show_all(float *axes, bool isMpos, bool *limits) {
    //_oled->clear();
    // log_info("OLED Show all");

    // Save off dro values here; otherwise with new menu regime we had no axes saved when entering jog menu
    saved_axes = axes;
    saved_isMpos = isMpos;
    saved_limits = limits;

    if ( (_menu->is_home_menu() || _menu->is_run_menu() || _menu->is_postrun_menu())
        && !(_state == "Alarm" || _state == "Run" || _state == "Hold:0" || _state == "Hold:1" || _download_mode || _file_job_running || _popup) ) {
        // in an icon menu, and not in a state where we don't show a menu at all
        render_icon_menu();
    } else {
        show_state();
        show_file();
        show_menu();
        if (!config->_i2c[0]->_fail_safe && ((sys.state != State::Jog) || (jog_state == JogState::Idle))) {  // Don't update dro when jogging to position using the encoder
            show_dro(axes, isMpos, limits);
        }
        show_radio_info();
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
        show_postrun_layout(selected);
    }
    _enc_scroll_lockout = false;  // Unlock scrolling to use menu (if needed)
}

void OLED::show_home_layout(int hightlight) {
//    log_info("show home layout");
    // clear entire screen and set text state
    _oled->setColor(BLACK);
    _oled->fillRect(0,0,_width,_height);
    _oled->setColor(WHITE);
    _oled->setTextAlignment(TEXT_ALIGN_LEFT);
    _oled->setFont(DejaVu_Sans_10);
    // top text
    if (_state == "Home") {
        show(stateLayout, "Homing...");
    } else {
        // Machine name as specified in config file
        show(stateLayout, config->_name.c_str());
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
        if (strncmp(config->_name.c_str(), "EggBot", 24) == 0) { // motor lock/unlock icon for Eggbot
            if (_motors_on) {
                _oled->drawXbm(52, 20, 24, 24, lock_icon_bits);
            } else {
                _oled->drawXbm(52, 20, 24, 24, unlock_icon_bits);
            }
        } else { // home icon for everything else
            _oled->drawXbm(52, 20, 24, 24, home_icon_bits);
        }
        _oled->setColor(WHITE);
    } else {
        if (strncmp(config->_name.c_str(), "EggBot", 24) == 0) { // motor lock icon for Eggbot
            if (_motors_on) {
                _oled->drawXbm(52, 20, 24, 24, lock_icon_bits);
            } else {
                _oled->drawXbm(52, 20, 24, 24, unlock_icon_bits);
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
            show(bottomTextLayout, entry->display_name);
        }
        entry = entry->next;
        i++;
    }
    // homed indication
    if (hightlight == 2) { // home icon selected
        if(strncmp(config->_name.c_str(), "EggBot", 24) == 0) { // special motor indication for Eggbot
            if (_motors_on) {
                show(bottomRightLayout, "(motors on)");
            } else {
                show(bottomRightLayout, "(motors off)");
            }
        } else { // standard homed check for all other machines
            if(config->_axes->_homed) {
                show(bottomRightLayout, "(homed)");
            } else {
                show(bottomRightLayout, "(unhomed)");
            }
        }
    }

    _oled->display();
}

void OLED::show_run_layout(int hightlight) {  // run menu
    log_info("Show run layout");
    // clear entire screen and set text state
    _oled->setColor(BLACK);
    _oled->fillRect(0,0,_width,_height);
    _oled->setColor(WHITE);
    _oled->setTextAlignment(TEXT_ALIGN_LEFT);
    _oled->setFont(DejaVu_Sans_10);
    // top text
    // get selected menu text
    ListNodeType *entry = _menu->get_active_head();
    int i = 0;
    while (entry && entry->display_name && i < 4) {
        if (entry->selected) {
            show(stateLayout, entry->display_name);
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

    _oled->display();
}

void OLED::show_postrun_layout(int hightlight) {  // run menu
    log_info("Show postrun layout");
    // clear run timer here to make sure it gets reset between repeated runs
    if (_saved_run_time == 0) {
        _prev_run_time += (millis() - _run_start_time);
        _saved_run_time = _prev_run_time / 1000;
        //log_info("Calc'd run time in postrun: " << _saved_run_time);
    }
    _run_start_time = 0;
    _prev_run_time = 0;
    // clear entire screen and set text state
    _oled->setColor(BLACK);
    _oled->fillRect(0,0,_width,_height);
    _oled->setColor(WHITE);
    _oled->setTextAlignment(TEXT_ALIGN_LEFT);
    _oled->setFont(DejaVu_Sans_10);
    // top text
    // get selected menu text
    ListNodeType *entry = _menu->get_active_head();
    int i = 0;
    while (entry && entry->display_name && i < 4) {
        if (entry->selected) {
            if (strncmp(entry->display_name, "< Back", 40) == 0) { // special case, override
                if (_menu->get_last_file_succeeded() ) {
                    // calc previous run time
                    char completed_msg[24];
                    snprintf(completed_msg, 24, "Completed in: %02d:%02d:%02d", 
                        (_saved_run_time / 3600),          // hours
                        ((_saved_run_time % 3600) / 60),   // minutes
                        ((_saved_run_time % 3600) % 60));  // seconds
                    show(stateLayout, completed_msg);
                } else {
                    show(stateLayout, "Plot Cancelled");
                }
            } else {
                show(stateLayout, entry->display_name);
            }
        }
        entry = entry->next;
        i++;
    }
    // two icons
    if (hightlight == 1) {
        _oled->fillRect(18, 18, 28, 28);
        _oled->setColor(BLACK);
        _oled->drawXbm(20, 20, 24, 24, left_icon_bits);
        _oled->setColor(WHITE);
    } else {
        _oled->drawXbm(20, 20, 24, 24, left_icon_bits);
    }
    if (hightlight == 2) {
        _oled->fillRect(82, 18, 28, 28);
        _oled->setColor(BLACK);
        _oled->drawXbm(84, 20, 24, 24, draw_icon_bits);
        _oled->setColor(WHITE);
    } else {
        _oled->drawXbm(84, 20, 24, 24, draw_icon_bits);
    }

    // bottom text : most recent file name
    show(bottomTextLayout, _menu->get_completed_file_name());

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
    
    if (menu_only) {
        show_menu();
    } else {
        show_all(saved_axes, saved_isMpos, saved_limits);
    }
}

// Display a popup message temporarily
void OLED::popup_msg(std::string msg, int dly) {

    // Show error message for 2s then restore display
    // Use flag to prevent other processes from updating screen
    _popup = true;
    show_error(msg);
    delay_ms(dly);  
    _popup = false;

    refresh_display();
}

void OLED::show_persistent_msg(std::string msg) {
    _popup = true;
    show_error(msg);
    // clear on user click or other call to clear_popup()
}

// manual clear for errors that stick around (like unexpected end of file)
void OLED::clear_popup() {
    log_info("OLED clear popup called");
    _popup = false;
    refresh_display();
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
    bool was_homing = (_state == "Home");
    // Now the string is a sequence of field|field|field
    size_t pos     = 0;
    auto   nextpos = _report.find_first_of("|", pos);
    _state         = _report.substr(pos + 1, nextpos - pos - 1);
    // check for finished homing
    if (was_homing && _state != "Home") {
        if (config->_axes->_homed) {
            log_info("Detected successful homing completion");
            if(_file_awaiting_homing.length() != 0) {
                clear_popup(); // clear homing before run message
                log_info("Running file after homing: " << _file_awaiting_homing);
                _menu->set_completed_file(_file_awaiting_homing.c_str()); // store run file path
                InputFile *infile = new InputFile("sd", _file_awaiting_homing.c_str(), WebUI::AuthenticationLevel::LEVEL_ADMIN, allChannels);
                allChannels.registration(infile);
                _file_awaiting_homing = "";
            }
        } else {
            log_info("Detected unsuccessful homing");
            if(_file_awaiting_homing.length() != 0) {
                clear_popup(); // clear homing before run message
                _file_awaiting_homing = "";
                show_error("Homing before file run unsuccessful, please try again.");
            }
        }
    }

    bool probe              = false;
    bool limits[MAX_N_AXIS] = { false };

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
    nextpos = _report.find_first_of(":", pos);
    pos = nextpos + 1; // past ERR
    _report = _report.substr(pos); // trim ERR
    _popup = true;
    show_error("Error " + _report); // popup error report until next button click
}

// [MSG:INFO: Connecting to STA:SSID foo]
void OLED::parse_STA() {
    size_t start = strlen("[MSG:INFO: Connecting to STA SSID:");
    _radio_info  = _report.substr(start, _report.size() - start - 1);

    auto fh = font_height(DejaVu_Sans_10);
//    show(connectWifiLayout, "Connecting to Wi-Fi..."); // conflicts with showing version on boot screen
    _oled->display();
}

// [MSG:INFO: Connected - IP is 192.168.68.134]
void OLED::parse_IP() {
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

void OLED::show_wifi_info() {
    // similar to above using stored info
    if( WebUI::wifi_config.isOn() ) {
        _oled->clear();
        wrapped_draw_string(0, "Wi-Fi Info", DejaVu_Sans_10);
        _oled->fillRect(0, _header_height - 2, _width, 2);  // Thick line
        wrapped_draw_string(_header_height, "Network ID: " + _radio_info, DejaVu_Sans_10); //DejaVu_Sans_Bold_10);
        wrapped_draw_string(_header_height*2, "IP Addr: " + _radio_addr, DejaVu_Sans_10);
        wrapped_draw_string(_header_height*3 + 4, "(Click to return)", DejaVu_Sans_10);
        _oled->display();
        _popup = true;
    } else {
        popup_msg("WiFi is off");
    }
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
        _job_just_started = true;
        _saved_run_time = 0;
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
size_t OLED::char_width(char c, font_t font) {
    xfont_t* xf    = (xfont_t*)font;
    int      index = c - xf->first;
    return (index < 0) ? 0 : xf->glyphs[index].width;
}

void OLED::wrapped_draw_string(int16_t y, const std::string& s, font_t font, bool setFont) {
    if (setFont) { // only want to do this once, not on recursion
        _oled->setFont(font);
        _oled->setTextAlignment(TEXT_ALIGN_LEFT);
    }

    if (y > _height) {
        // we've wrapped down past the bottom of the screen; bail.
        return;
    }

    size_t slen   = s.length();
    size_t swidth = 0;
    size_t i;
    size_t lastSpace = 0;
    for (i = 0; i < slen && swidth < _width; i++) {
        swidth += char_width(s[i], font);
        if (s[i] == ' ') { lastSpace = i; }
        if (swidth > _width) {
            break;
        }
    }
    if (swidth < _width) {
        _oled->drawString(0, y, s.c_str());
    } else {
        if (lastSpace == 0) { // no spaces found in this entire screen width, break at character
            _oled->drawString(0, y, s.substr(0, i).c_str());
            wrapped_draw_string(y + font_height(font) - 1, s.substr(i, slen).c_str(), font, false);
        } else { // break at most recent space
            _oled->drawString(0, y, s.substr(0, lastSpace).c_str());
            // +1 on recursion to skip the space
            wrapped_draw_string(y + font_height(font) - 1, s.substr(lastSpace+1, slen).c_str(), font, false);
        }
    }
}

void OLED::truncated_draw_string(int16_t y, const std::string& s, font_t font) {
    _oled->setFont(font);
    _oled->setTextAlignment(TEXT_ALIGN_LEFT);

    std::string dots = "...";
    size_t dots_width = char_width(dots[0] * dots.length(), font);

    size_t slen   = s.length();
    size_t swidth = 0;
    size_t i;
    for (i = 0; i < slen && swidth < _width; i++) {
        swidth += char_width(s[i], font);
        if (swidth > (_width - dots_width)) {
            break;
        }
    }
    if (swidth < (_width - dots_width)) {
        _oled->drawString(0, y, s.c_str());
    } else {
        _oled->drawString(0, y, s.substr(0, i).c_str());
        _oled->drawString((_width - dots_width), y, dots.c_str()); // Ellipsis dots for truncation
    }
}

void OLED::draw_checkbox(int16_t x, int16_t y, int16_t width, int16_t height, bool checked) {
    if (checked) {
        _oled->fillRect(x, y, width, height);  // If log.0
    } else {
        _oled->drawRect(x, y, width, height);  // If log.1
    }
}
