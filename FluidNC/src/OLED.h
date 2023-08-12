#pragma once

#include "esp_timer.h"
#include "Config.h"
#include "Configuration/Configurable.h"
#include "Channel.h"
#include "SSD1306_I2C.h"
#include "Limits.h"
#include "Menu.h"

// Jogging settings
#define JOG_X_STEP              1.0
#define JOG_Y_STEP              1.0
#define JOG_Z_STEP              0.5
#define JOG_TIMER_MS            250
#define JOG_FEEDRATE            1000.0

typedef const uint8_t* font_t;

// Jogging states
enum class JogState : uint8_t {
    Idle = 0,   // Not jogging
    Scrolling,  // Scrolling to jog value with encoder
    Jogging,    // Performing the jog command
};

class OLED : public Channel, public Configuration::Configurable {
public:

    struct Layout {
        uint8_t                    _x;
        uint8_t                    _y;
        uint8_t                    _width_required;
        font_t                     _font;
        OLEDDISPLAY_TEXT_ALIGNMENT _align;
    };
    static Layout bannerLayout128;
    static Layout bannerLayout64;
    static Layout stateLayout;
    static Layout elapsedTimeLayout;
    static Layout filenameLayout;
    static Layout percentLayout128;
    static Layout percentLayout64;
    static Layout posLabelLayout;
    static Layout radioAddrLayout;
    static Layout connectWifiLayout;

private:

    std::string _report;

    std::string _radio_info;
    std::string _radio_addr;

    std::string _state;
    std::string _filename;

    float       _percent;
    uint32_t    _run_start_time = 0;
    uint32_t    _prev_run_time = 0;

    int _radio_delay = 0;

    uint8_t _i2c_num = 0;

    int _enc_diff = 0;
    
    void parse_report();
    void parse_status_report();
    void parse_gcode_report();
    void parse_STA();
    void parse_IP();
    void parse_AP();
    void parse_BT();
    void parse_encoder();

    void parse_axes(std::string s, float* axes);
    void parse_numbers(std::string s, float* nums, int maxnums);

    void show_limits(bool probe, const bool* limits);
    void show_state();
    void show_menu();
    void show_file();
    void show_dro(float* axes, bool isMpos, bool* limits);
    void show_radio_info();
    void show_error(String);
    void show_all(float *axes, bool isMpos, bool *limits);

    void draw_checkbox(int16_t x, int16_t y, int16_t width, int16_t height, bool checked);

    void wrapped_draw_string(int16_t y, const std::string& s, font_t font);
    void truncated_draw_string(int16_t y, const std::string& s, font_t font);

    void show(Layout& layout, const std::string& msg) { show(layout, msg.c_str()); }
    void show(Layout& layout, const char* msg);

    uint8_t font_width(font_t font);
    uint8_t font_height(font_t font);
    size_t  char_width(char s, font_t font);

    OLEDDISPLAY_GEOMETRY _geometry = GEOMETRY_128_64;

    bool _error = false;
    bool _active = false;

public:
    OLED() : Channel("oled") {}

    OLED(const OLED&) = delete;
    OLED(OLED&&)      = delete;
    OLED& operator=(const OLED&) = delete;
    OLED& operator=(OLED&&) = delete;

    virtual ~OLED() = default;

    void init();
    void refresh_display(bool menu_only = false);
    void popup_msg(String msg);
    JogState get_jog_state();
    void set_jog_state(JogState);
    bool is_active();

    OLEDDisplay* _oled;
    Menu* _menu = new Menu();

    // Configurable

    uint8_t _address = 0x3c;
    int     _width   = 64;
    int     _height  = 48;

    // Channel method overrides

    size_t write(uint8_t data) override;

    int read(void) override { return -1; }
    int peek(void) override { return -1; }

    Channel* pollLine(char* line) override;
    void     flushRx() override {}

    bool   lineComplete(char*, char) override { return false; }
    size_t timedReadBytes(char* buffer, size_t length, TickType_t timeout) override { return 0; }

    // Configuration handlers:
    void validate() override {}

    void afterParse() override;

    void group(Configuration::HandlerBase& handler) override {
        handler.item("i2c_num", _i2c_num);
        handler.item("i2c_address", _address);
        handler.item("width", _width);
        handler.item("height", _height);
        handler.item("radio_delay_ms", _radio_delay);
    }
};
