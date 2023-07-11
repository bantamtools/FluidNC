#include "OLED.h"

#include "Machine/MachineConfig.h"

// Static variables
static float* saved_axes = NULL;   // Saved dro values for refreshing display
static bool saved_isMpos = false;
static bool* saved_limits = NULL;

static volatile JogState jog_state;
static volatile bool jog_timer_active;

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

// Returns true if the current menu is the files menu
 bool OLED::menu_is_files_list(void) {
    return (current_menu == files_menu);
}

// Returns the selected entry
struct MenuNodeType *OLED::menu_get_selected(void) {

    // Traverse the list and print out each menu entry name
    MenuNodeType *entry = current_menu->active_head; // Start at the beginning of the active window
    int i = 0;
    while (entry) {

        // Found selected entry
        if (entry->selected)
            break;

        // Advance the line and pointer
        entry = entry->next;
    }

    return entry;
}

// Helper function to enter a submenu
void OLED::menu_enter_submenu(void) {

    MenuNodeType *selected_entry = this->menu_get_selected();

    // Check if entry has a submenu
    if (selected_entry->child) {

        // Make the submenu active
        current_menu = selected_entry->child;      

        // Populate the files list if SD file menu
        if (current_menu == files_menu) {

            menu_populate_files_list();
        }   
        
        // Refresh the display
        show_all(saved_axes, saved_isMpos, saved_limits);
    }
}

// Helper function to exit a submenu
void OLED::menu_exit_submenu(void) {

    // Check if submenu has an upper menu
    if (current_menu->parent) {
    
        // Make the upper menu active
        current_menu = current_menu->parent;

        // Refresh the display
        show_all(saved_axes, saved_isMpos, saved_limits);
    }
}

// Get the jogging state
JogState OLED::menu_get_jog_state(void) {
    return jog_state;
}

// Set the jogging state
void OLED::menu_set_jog_state(JogState state) {
    jog_state = state;
}

// Display an error message temporarily
void OLED::menu_show_error(String msg) {

    // Show error message for 2s then restore display
    show_error(msg);
    delay_ms(2000);
    show_all(saved_axes, saved_isMpos, saved_limits);
}

// Helper function to return the active tail
struct MenuNodeType *OLED::menu_get_active_tail(MenuType *menu, int menu_max_active_entries) {

    bool active_area = false;
    struct MenuNodeType *entry;
    int num_active_nodes = 0;

    // Traverse the linked list
    entry = menu->head;  // Reset to head
    while (entry->next && num_active_nodes < (menu_max_active_entries - 1)) {

        // Count the active window nodes
        if (entry == menu->active_head) {
            active_area = true;
        }
        if (active_area) {
          num_active_nodes++;  
        }

        // Go to the next entry
        entry = entry->next;
    }
    return entry;
}

// Initializes a menu with default settings
void OLED::menu_initialize(MenuType *menu, MenuType *parent) {
    
    // Initialize the menu to empty with no active window
    menu->head = menu->active_head = NULL;

    // Set the parent menu if one exists
    menu->parent = parent;
}

// Adds a node entry to the given menu
void OLED::menu_add(MenuType *menu, MenuType *submenu, const char *path, const char *display_name) {

    // Allocate memory for the new entry
    struct MenuNodeType* new_entry = (MenuNodeType*)malloc(sizeof(struct MenuNodeType));

    // Populate the entry
    new_entry->prev = NULL;
    new_entry->next = NULL;

    new_entry->child = submenu;

    if (display_name) strncpy(new_entry->display_name, display_name, MENU_NAME_MAX_STR);    // Cuts off long display names
    if (path) strncpy(new_entry->path, path, MENU_NAME_MAX_PATH);                           // Cuts off long file paths
    new_entry->selected = false;

    // No menu entries, insert as the head, set as active window head and select it
    if (menu->head == NULL) {
        new_entry->prev = NULL;
        new_entry->selected = true;
        menu->head = new_entry;
        menu->active_head = new_entry;
        return;
    }

    // List not empty, traverse to the end to add menu item
    struct MenuNodeType* temp = menu->head;

    // Looking for tail
    while (temp->next != NULL) {
        temp = temp->next;
    }

    // Add menu item at the tail
    temp->next = new_entry;
    new_entry->prev = temp;
}

// Deletes all nodes in the given menu
void OLED::menu_delete(MenuType *menu) {

    struct MenuNodeType* entry = menu->head;

    // Traverse the menu until empty, clearing the memory for the nodes
    while(entry) {

        // Attach menu head to next node
        menu->head = entry->next;

        // Set new node to head unless it's empty
        if (menu->head) {
            menu->head->prev = NULL;
        }

        // Free the old node memory
        free(entry);
        entry = NULL;

        // Advance the pointer
        entry = menu->head;
    }

    // Mark the head and active head NULL to prevent use-after-free
    menu->head = menu->active_head = NULL;
}

// Populates the file list from SD card
void OLED::menu_populate_files_list(void) {

    MenuNodeType *entry = current_menu->head;  // Start at the top of the active menu
    
    // If not in the files menu, find the node attached the the files menu
    while (current_menu != files_menu && entry) {

        // Found the load files node
        if (entry->child == files_menu) {
            break;
        }

        // Advance the pointer
        entry = entry->next;
    }

    // Get the file listing
    FileListType *files = sd_get_filelist();

    // Clear out the menu nodes if they already exist
    if ((current_menu == files_menu && current_menu->head) ||
        (current_menu != files_menu && entry->child->head)) {
        menu_delete(files_menu);
    }

    // Add the back button to top of menu
    menu_add(this->files_menu, NULL, NULL, "< Back");

    // Create a submenu of files
    for (auto i = 0; i < files->num_files; i++) {

        // Extract the display name from the full path
        char *filename = strrchr(files->path[i], '/') + 1;
        
        // Initialize the files menu and attach nodes
        menu_add(files_menu, NULL, files->path[i], filename);
    }
}

// Initializes the menu subsystem
void OLED::menu_init(void) {

    // Allocate memory for the menus
    main_menu = (MenuType*)malloc(sizeof(struct MenuType));
    files_menu = (MenuType*)malloc(sizeof(struct MenuType));
    jogging_menu = (MenuType*)malloc(sizeof(struct MenuType));
    settings_menu = (MenuType*)malloc(sizeof(struct MenuType));
    version_menu = (MenuType*)malloc(sizeof(struct MenuType));

    // Initialize the menus
    menu_initialize(main_menu, NULL);
    menu_initialize(files_menu, main_menu);
    menu_initialize(jogging_menu, main_menu);
    menu_initialize(settings_menu, main_menu);
    menu_initialize(version_menu, settings_menu);

    // Set main menu as current
    current_menu = main_menu;

    // Main Menu
    menu_add(main_menu, NULL, NULL, "Home");
    menu_add(main_menu, jogging_menu, NULL, "Jogging");
    menu_add(main_menu, files_menu, NULL, "Run from SD");
    menu_add(main_menu, settings_menu, NULL, "Settings");

    // Jogging Menu
    menu_add(jogging_menu, NULL, NULL, "< Back");
    menu_add(jogging_menu, NULL, NULL, "Jog X");
    menu_add(jogging_menu, NULL, NULL, "Jog Y");
    menu_add(jogging_menu, NULL, NULL, "Jog Z");

    // Settings Menu
    menu_add(settings_menu, NULL, NULL, "< Back");
    //menu_add(settings_menu, NULL, NULL, "Update");  // WebUI already includes OTA functionality
    menu_add(settings_menu, version_menu, NULL, "Version");

    // Version Menu
    char bantam_ver_str[MENU_NAME_MAX_STR] = {"Version: "};
    strncat(bantam_ver_str, git_info_short, MENU_NAME_MAX_STR - 10);
    char fluidnc_ver_str[MENU_NAME_MAX_STR] = {"FluidNC: "};
    strncat(fluidnc_ver_str, fluidnc_version, MENU_NAME_MAX_STR - 10);

    menu_add(version_menu, NULL, NULL, "< Back");
    menu_add(version_menu, NULL, NULL, bantam_ver_str);
    menu_add(version_menu, NULL, NULL, fluidnc_ver_str);

    // Not jogging at init
    jog_state = JogState::Idle;
}

// Updates the current menu selection
void OLED::menu_update_selection(int menu_max_active_entries) {

    MenuNodeType *entry = current_menu->head;  // Start at the top of the active menu
    MenuNodeType *active_tail;

    // Lock out scrolling during operation
    if (sys.state != State::Idle) {
        return;
    }
    
    while (entry) {
        
        // Found selected entry and we have scrolled
        if (entry->selected && (this->enc_diff != 0)) {

            // Adjust menu selection and active window as needed
            if (this->enc_diff > 0 && entry->next != NULL) {        // Forwards until hit tail
                entry->next->selected = true;
                entry->selected = false;
                active_tail = this->menu_get_active_tail(current_menu, menu_max_active_entries);
                if (active_tail->next && active_tail->next->selected) {  // Shift the window once scroll past max entries
                    current_menu->active_head = current_menu->active_head->next;
                }

            } else if (this->enc_diff < 0 && entry->prev != NULL) { // Backwards until hit head
                entry->prev->selected = true;
                entry->selected = false;
                if (current_menu->active_head->prev && current_menu->active_head->prev->selected) {  // Shift the window once scroll past max entries
                    current_menu->active_head = current_menu->active_head->prev;
                }
            }
            break;

        // No update or operation in progress, set current selection entry
        } else if (entry->selected) {
            break;
        }
        entry = entry->next;      
    }
}

void OLED::show(Layout& layout, const char* msg) {
    if (_width < layout._width_required) {
        return;
    }
    _oled->setTextAlignment(layout._align);
    _oled->setFont(layout._font);
    _oled->drawString(layout._x, layout._y, msg);
}

OLED::Layout OLED::bannerLayout128  = { 0, 0, 0, ArialMT_Plain_16, TEXT_ALIGN_CENTER };
OLED::Layout OLED::bannerLayout64   = { 0, 0, 0, ArialMT_Plain_16, TEXT_ALIGN_CENTER };
OLED::Layout OLED::stateLayout      = { 0, 0, 0, ArialMT_Plain_10, TEXT_ALIGN_LEFT };
OLED::Layout OLED::tickerLayout     = { 63, 0, 128, ArialMT_Plain_10, TEXT_ALIGN_CENTER };
OLED::Layout OLED::filenameLayout   = { 63, 13, 128, ArialMT_Plain_10, TEXT_ALIGN_CENTER };
OLED::Layout OLED::percentLayout128 = { 128, 0, 128, ArialMT_Plain_10, TEXT_ALIGN_RIGHT };
OLED::Layout OLED::percentLayout64  = { 64, 0, 64, ArialMT_Plain_10, TEXT_ALIGN_RIGHT };
OLED::Layout OLED::posLabelLayout   = { 110, 13, 128, ArialMT_Plain_10, TEXT_ALIGN_RIGHT };
OLED::Layout OLED::radioAddrLayout  = { 50, 0, 128, ArialMT_Plain_10, TEXT_ALIGN_LEFT };

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
    log_info("OLED I2C address:" << to_hex(_address) << " width: " << _width << " height: " << _height);
    _oled = new SSD1306_I2C(_address, _geometry, config->_i2c[_i2c_num], 400000);
    _oled->init();

    _oled->flipScreenVertically();
    _oled->setTextAlignment(TEXT_ALIGN_LEFT);

    _oled->clear();

    show((_width == 128) ? bannerLayout128 : bannerLayout64, "Bantam Tools");

    _oled->display();

    allChannels.registration(this);
    setReportInterval(500);

    this->menu_init();
}

Channel* OLED::pollLine(char* line) {
    autoReport();
    return nullptr;
}

void OLED::show_state() {

    // Clear anything left in radio area
    _oled->setColor(BLACK);
    _oled->fillRect(0, 0, 50, 11);
    _oled->setColor(WHITE);

    show(stateLayout, _state);
    _oled->drawLine(0, 11, 128, 11);
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

    int16_t menu_width;
    int16_t menu_height;
    int menu_max_active_entries;

    // Don't show menu during Alarm, Run or Hold states
    if (_state == "Alarm" || _state == "Run" || _state == "Hold:0" || _state == "Hold:1") {
        return;
    }

    _oled->setTextAlignment(TEXT_ALIGN_LEFT);

    // Set up font and menu window
    _oled->setFont(ArialMT_Plain_10);
    menu_height = 13;
    (current_menu == files_menu || current_menu == version_menu) ? menu_width = 128 : menu_width = 64;
    menu_max_active_entries = 4;

    // Clear any highlighting left in menu area
    _oled->setColor(BLACK);
    _oled->fillRect(0, 13, menu_width, 64);
    _oled->setColor(WHITE);

    // Update the menu selection if not jogging
    if (jog_state == JogState::Idle) {

        menu_update_selection(menu_max_active_entries);
    }

    // Traverse the list and print out each menu entry name
    MenuNodeType *entry = current_menu->active_head; // Start at the beginning of the active window
    int i = 0;
    while (entry && entry->display_name && i < menu_max_active_entries) {

        // Highlight selected entry
        (entry->selected) ? _oled->setColor(WHITE) : _oled->setColor(BLACK);
        _oled->fillRect(0, 13 + (menu_height * i), menu_width, menu_height);
        (entry->selected) ? _oled->setColor(BLACK) : _oled->setColor(WHITE);

        // Write out the entry name
        _oled->drawString(0, 13 + (menu_height * i), (String)(entry->display_name));

        // Advance the line and pointer
        entry = entry->next;
        i++;
    }
    _oled->display();
}

void OLED::show_file() {
    int pct = int(_percent);
    if (_filename.length() == 0) {
        return;
    }
    if (_state != "Run" && pct == 100) {
        // This handles the case where the system returns to idle
        // but shows one last SD report
        return;
    }

    // Clear anything left in file areas
    _oled->setColor(BLACK);
    _oled->fillRect(50, 0, 78, 11);
    _oled->fillRect(0, 13, 128, 64);
    _oled->setColor(WHITE);

    if (_width == 128) {
        show(percentLayout128, std::to_string(pct) + '%');

        _ticker += "-";
        if (_ticker.length() >= 12) {
            _ticker = "-";
        }
        show(tickerLayout, _ticker);

        wrapped_draw_string(14, _filename, ArialMT_Plain_10);

        _oled->drawProgressBar(0, 39, 120, 10, pct);
    } else {
        show(percentLayout64, std::to_string(pct) + '%');
    }

    // Display pause/resume message at bottom
    if (_state == "Hold:0" || _state == "Hold:1") {
        _oled->drawString(0, 52, "Press button to RESUME");
    } else {
        _oled->drawString(0, 52, "Press button to PAUSE");
    }
}
void OLED::show_dro(float* axes, bool isMpos, bool* limits) {

    // Save off dro values in case we need to refresh display with current data
    saved_axes = axes;
    saved_isMpos = isMpos;
    saved_limits = limits;

    if (_state == "Alarm" || _state == "Hold:0" || _state == "Hold:1" ||
        current_menu == files_menu || current_menu == version_menu) {
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
    _oled->fillRect(64, 13, 64, 64);
    _oled->setColor(WHITE);

    show(posLabelLayout, isMpos ? "M Pos" : "W Pos");

    _oled->setFont(ArialMT_Plain_10);
    uint8_t oled_y_pos;
    for (uint8_t axis = X_AXIS; axis < n_axis; axis++) {
        oled_y_pos = ((_height == 64) ? 24 : 17) + (axis * 10);

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
    if ((_state == "Run" && _filename.length()) || _state == "Hold:0" || _state == "Hold:1") {
        return;
    }

    // Clear anything left in radio area
    _oled->setColor(BLACK);
    _oled->fillRect(50, 0, 78, 11);
    _oled->setColor(WHITE);

    if (_width == 128) {
        if (_state == "Alarm") {
            show_error("Press button to CLEAR");
        } else if (_state != "Run") {
            show(radioAddrLayout, _radio_addr);
        }
    } else {
        if (_state == "Alarm") {
            show_error("Press button to CLEAR");
        }
    }
}

void OLED::show_error(String msg) {

    // Clear anything left in error message area
    _oled->setColor(BLACK);
    _oled->fillRect(0, 13, 128, 64);
    _oled->setColor(WHITE);

    // Draw message
    _oled->setFont(ArialMT_Plain_10);
    _oled->drawString(0, 13, msg);
    _oled->display();
}

void OLED::show_all(float *axes, bool isMpos, bool *limits) {

    //_oled->clear();
    show_state();
    show_file();
    show_menu();
    if (((sys.state != State::Jog)) || (jog_state == JogState::Idle)) {  // Don't update dro when jogging to position using the encoder
        show_dro(axes, isMpos, limits);
    }
    show_radio_info();
    _oled->display();
}

void OLED::parse_numbers(std::string s, float* nums, int maxnums) {
    size_t pos     = 0;
    size_t nextpos = -1;
    size_t i;
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
    _state         = _report.substr(pos + 1, nextpos - pos - 1);

    bool probe              = false;
    bool limits[MAX_N_AXIS] = { false };

    static float axes[MAX_N_AXIS];
    bool  isMpos = false;
    _filename    = "";

    // ... handle it
    while (nextpos != std::string::npos) {
        pos        = nextpos + 1;
        nextpos    = _report.find_first_of("|", pos);
        auto field = _report.substr(pos, nextpos - pos);
        // MPos:, WPos:, Bf:, Ln:, FS:, Pn:, WCO:, Ov:, A:, SD: (ISRs:, Heap:)
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
        if (tag == "SD") {
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

// [MSG:INFO: Connecting to STA:SSID foo]
void OLED::parse_STA() {
    size_t start = strlen("[MSG:INFO: Connecting to STA SSID:");
    _radio_info  = _report.substr(start, _report.size() - start - 1);

    _oled->clear();
    auto fh = font_height(ArialMT_Plain_10);
    wrapped_draw_string(0, _radio_info, ArialMT_Plain_10);
    _oled->display();
}

// [MSG:INFO: Connected - IP is 192.168.68.134]
void OLED::parse_IP() {
    size_t start = _report.rfind(" ") + 1;
    _radio_addr  = _report.substr(start, _report.size() - start - 1);

    _oled->clear();
    auto fh = font_height(ArialMT_Plain_10);
    wrapped_draw_string(0, _radio_info, ArialMT_Plain_10);
    wrapped_draw_string(fh * 2, _radio_addr, ArialMT_Plain_10);
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
    auto fh = font_height(ArialMT_Plain_10);
    wrapped_draw_string(0, _radio_info, ArialMT_Plain_10);
    wrapped_draw_string(fh * 2, _radio_addr, ArialMT_Plain_10);
    _oled->display();
    delay_ms(_radio_delay);
}

void OLED::parse_BT() {
    size_t      start  = strlen("[MSG:INFO: BT Started with ");
    std::string btname = _report.substr(start, _report.size() - start - 1);
    _radio_info        = "BT: ";
    _radio_info += btname.c_str();

    _oled->clear();
    wrapped_draw_string(0, _radio_info, ArialMT_Plain_10);
    _oled->display();
    delay_ms(_radio_delay);
}

//[MSG:INFO: Encoder difference -> 1]
void OLED::parse_encoder() {

    size_t  start   = strlen("[MSG:INFO: Encoder difference -> ");
    size_t  end     = _report.rfind("]");
    
    // Save off the encoder difference to update the menu
    this->enc_diff = stoi(_report.substr(start, end - start));

    // System IDLE and scrolling to jog
    if ((sys.state == State::Idle) && (jog_state == JogState::Scrolling)) {

        // Extract axis from menu item
        char *axis = (strrchr(menu_get_selected()->display_name, ' ') + 1);

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
            
                saved_axes[X_AXIS] += (JOG_X_STEP * (float)enc_diff); 
                if (saved_axes[X_AXIS] < limitsMinPosition(X_AXIS)) saved_axes[X_AXIS] = limitsMinPosition(X_AXIS);
                if (saved_axes[X_AXIS] > limitsMaxPosition(X_AXIS)) saved_axes[X_AXIS] = limitsMaxPosition(X_AXIS);
                break;
            
            case 'Y': 
            
                saved_axes[Y_AXIS] += (JOG_Y_STEP * (float)enc_diff);
                if (saved_axes[Y_AXIS] < limitsMinPosition(Y_AXIS)) saved_axes[Y_AXIS] = limitsMinPosition(Y_AXIS);
                if (saved_axes[Y_AXIS] > limitsMaxPosition(Y_AXIS)) saved_axes[Y_AXIS] = limitsMaxPosition(Y_AXIS);
                break;

            case 'Z': 
            
                saved_axes[Z_AXIS] += (JOG_Z_STEP * (float)enc_diff); 
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

void OLED::parse_report() {
    if (_report.length() == 0) {
        return;
    }
    if (_report.rfind("<", 0) == 0) {
        parse_status_report();
        return;
    }
    if (_report.rfind("[GC:", 0) == 0) {
        parse_gcode_report();
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
    if (_report.rfind("[MSG:INFO: Encoder difference -> ", 0) == 0) {
        parse_encoder();
        return;
    }

    // Refresh the screen on card detect event to update file list
     if ((_report.rfind("[MSG:INFO: SD Card Detect Event]", 0) == 0) && (current_menu == files_menu)) {
        menu_populate_files_list();
        show_menu();
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

void OLED::wrapped_draw_string(int16_t y, const std::string& s, font_t font) {
    _oled->setFont(font);
    _oled->setTextAlignment(TEXT_ALIGN_LEFT);

    size_t slen   = s.length();
    size_t swidth = 0;
    size_t i;
    for (i = 0; i < slen && swidth < _width; i++) {
        swidth += char_width(s[i], font);
        if (swidth > _width) {
            break;
        }
    }
    if (swidth < _width) {
        _oled->drawString(0, y, s.c_str());
    } else {
        _oled->drawString(0, y, s.substr(0, i).c_str());
        _oled->drawString(0, y + font_height(font) - 1, s.substr(i, slen).c_str());
    }
}

void OLED::draw_checkbox(int16_t x, int16_t y, int16_t width, int16_t height, bool checked) {
    if (checked) {
        _oled->fillRect(x, y, width, height);  // If log.0
    } else {
        _oled->drawRect(x, y, width, height);  // If log.1
    }
}
