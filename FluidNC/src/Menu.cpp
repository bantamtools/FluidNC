#include "Menu.h"
#include "Machine/MachineConfig.h"
#include "WebUI/WifiConfig.h"

// Constructor
Menu::Menu() {

    // Allocate memory for the menus
    _main_menu = new struct ListType;
    _run_menu = new struct ListType;
    _files_menu = new struct ListType;
    _jogging_menu = new struct ListType;
    // _rss_menu is handled by RSSReader
    _settings_menu = new struct ListType;
    _version_menu = new struct ListType;
    _postrun_menu = new struct ListType;
    _firmware_menu = new struct ListType;
    _config_menu = new struct ListType;
    _confirm_menu = new struct ListType;
    // _fw_update_menu = new struct ListType;

    // Initialize the menus
    init(_main_menu, NULL);
    init(_run_menu, _main_menu); // unused currently
    init(_files_menu, _main_menu);
    init(_settings_menu, _main_menu);
    init(_jogging_menu, _settings_menu);
    init(_version_menu, _settings_menu);
    init(_postrun_menu, _main_menu);
    init(_firmware_menu, _settings_menu);
    init(_config_menu, _settings_menu);
    init(_confirm_menu, _settings_menu);

    // Set main menu as current
    _current_menu = _main_menu;

    _recent_file_is_new_upload = false;

    // Build the initial menu
    build();
}

// Destructor
Menu::~Menu() {

    // Set main menu as none
    _current_menu = nullptr;

    // Remove all the menu nodes
    remove_entries(_version_menu);
    remove_entries(_jogging_menu);
    remove_entries(_files_menu);
    remove_entries(_firmware_menu);
    remove_entries(_config_menu);
    remove_entries(_settings_menu);
    remove_entries(_run_menu);
    remove_entries(_main_menu);
    remove_entries(_postrun_menu);
    remove_entries(_confirm_menu);

    // Deallocate memory for the menus
    delete(_version_menu);
    delete(_jogging_menu);
    delete(_files_menu);
    delete(_firmware_menu);
    delete(_config_menu);
    delete(_settings_menu);
    delete(_run_menu);
    delete(_main_menu);
    delete(_postrun_menu);
    delete(_confirm_menu);
}

// Returns true if the current menu is the files menu
 bool Menu::is_files_menu(void) {
    return (_current_menu == _files_menu);
}

// Returns true if the current menu is the RSS menu
 bool Menu::is_rss_menu(void) {
    return (_current_menu == _rss_menu);
}

bool Menu::is_home_menu(void) {
    return (_current_menu == _main_menu);
}
bool Menu::is_run_menu(void) {
    return (_current_menu == _run_menu);
}
bool Menu::is_settings_menu(void) {
    return (_current_menu == _settings_menu);
}
bool Menu::is_version_menu(void) {
    return (_current_menu == _version_menu);
}
bool Menu::is_postrun_menu(void) {
    return (_current_menu == _postrun_menu);
}
bool Menu::is_firmware_menu(void){
    return (_current_menu == _firmware_menu);
}
bool Menu::is_config_menu(void){
    return (_current_menu == _config_menu);
}
bool Menu::is_confirm_menu(void) {
    return (_current_menu == _confirm_menu);
}

// Connects the RSS feed to the menu system
void Menu::connect_rss_feed(ListType *feed) {

    // Hook up RSS menu to feed directly
    _rss_menu = feed;

    // Initialize and connect into menu system
    init(_rss_menu, _settings_menu);
    add_entry(_settings_menu, _rss_menu, NULL, "RSS Feed");
    add_entry(_rss_menu, NULL, NULL, "< Back");
}

void Menu::print_current_menu() {
    if(is_files_menu()){
        log_info("Files Menu");
    } else if (is_firmware_menu()){
        log_info("Firmware Menu");
    } else if (is_config_menu()){
        log_info("Config Menu");
    } else if (is_home_menu()) {
        log_info("Home Menu");
    } else if (is_postrun_menu()) {
        log_info("Postrun Menu");
    } else if (is_rss_menu()){
        log_info("RSS Menu");
    } else if (is_settings_menu()){
        log_info("Settings Menu");
    } else {
        log_info("Some other menu");
    }
}

// Returns the active menu head
struct ListNodeType *Menu::get_active_head(void) {
    return _current_menu->active_head;
}

// Returns the selected entry
struct ListNodeType *Menu::get_selected(void) {

    // Traverse the list and print out each menu entry name
    ListNodeType *entry = _current_menu->active_head; // Start at the beginning of the active window
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
void Menu::enter_submenu(void) {

    ListNodeType *selected_entry = get_selected();

    // Check if entry has a submenu
    if (selected_entry->child) {

        // Make the submenu active
        _current_menu = selected_entry->child;      

        // Special case for files list, select first file instead of "Back"
        if (is_files_menu()) {
            if(_files_menu->head->next == NULL){ // Check if entry after back is null or not. If null, present message, else, enter file menu.
                log_info("No files detected on SD");
                config->_oled->show_persistent_msg("No files present.                 Check SD");
                config->_oled->_menu->exit_submenu();
            } else {
                update_selection(4, 1); // move forward by one
            }
        }

        // Refresh the display
        if (config->_oled) {
            config->_oled->refresh_display();
        }
    }
}

// Helper function to exit a submenu
void Menu::exit_submenu(void) {

    // Check if submenu has an upper menu
    if (_current_menu->parent) {
    
        // Make the upper menu active
        _current_menu = _current_menu->parent;

        // Refresh the display
        if (config->_oled) {
            config->_oled->refresh_display();
        }
    }
}

void Menu::return_to_run_menu() {
    _current_menu = _run_menu;
    // Refresh the display
    if (config->_oled) {
        config->_oled->refresh_display();
    }
}
void Menu::go_to_postrun_menu() {
    _current_menu = _postrun_menu;
    // OLED already refreshes when it calls this
    if(config->_oled){
        config->_oled->refresh_display();
    }
}

void Menu::go_to_files_menu() {
    _current_menu = _files_menu;

    if(config->_oled){
        config->_oled->refresh_display();
    }
}
 
// Helper function to return the active tail
struct ListNodeType *Menu::get_active_tail(ListType *menu, int max_active_entries) {

    bool active_area = false;
    struct ListNodeType *entry;
    int num_active_nodes = 0;

    // Traverse the linked list
    entry = menu->head;  // Reset to head
    while (entry->next && num_active_nodes < (max_active_entries - 1)) {

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

// Helper function to add directory to files menu
ListType* Menu::add_directory(char *path, bool isBin, bool isCfg) {
    //char *path_copy = strdup(path);
    char *token = strtok(path, "/");
    ListType *current_menu;
    if(isBin){
        current_menu = _firmware_menu;
    } else if(isCfg) {
        current_menu = _config_menu;
    } else {
        current_menu = _files_menu;
    }
   

    while (token != NULL) {
        bool found = false;

        char token_copy[80];
        token_copy[0] = '\0'; // clear any data from previous loop
        strncat(token_copy, token, 76);
        strncat(token_copy, "/", 2); // append '/' to folder name in menu

        for (ListNodeType *entry = current_menu->head; entry != NULL; entry = entry->next) {
            if (strcmp(entry->display_name, token_copy) == 0 && entry->child != NULL) {
                current_menu = entry->child;
                found = true;
                break;
            }
        }

        // advance token here so we can test whether we're looking at the final (file) token
        token = strtok(NULL, "/");

        //if (!found && strstr(token, ".gcode") == NULL) { // only make new menu if we're not at the .gcode file at the end
        if (!found && token != NULL) { // only make new menu if we're not at the file at the end
            ListType *new_menu = new ListType;
            init(new_menu, current_menu);
            // Add a "Back" button at the start of each new submenu
            prep(new_menu);
//            log_info("Adding menu entry for folder: " << token_copy);
            add_entry(current_menu, new_menu, NULL, token_copy);
            current_menu = new_menu;
        }

        //free(token_copy);
        //token = strtok(NULL, "/");
    }

    //free(path_copy);
    return current_menu;
}


// Updated function to add SD file to files menu with directory structure
bool Menu::add_sd_file(char *path, bool isBin, bool isCfg) {
//    log_info("add_sd_file initial path: " << path);

    // Filter out trashed/hidden files and folders
    if (strncmp(path, "/.", 2) == 0) { // entire path (initial folder) starts with '.'
        //log_info("Discarded hidden path: " << path);
        return false;
    }

    // Create directory structure in the menu
    char *path_copy = strdup(path);
    ListType *file_menu = add_directory(path_copy, isBin, isCfg);
    free(path_copy);

    // Extract the display name from the full path
    char *filename = strrchr(path, '/') + 1;

    // Filter again for trashed/hidden files
    if (strncmp(filename, ".", 1) == 0) { // file starts with '.'
//        log_info("Discarded hidden file: " << path);
        return false;
    }

//    log_info("Adding menu entry for filepath: " << path);
    // Add the file to the correct submenu
    add_entry(file_menu, NULL, path, filename);
//    add_entry(_files_menu, NULL, path, filename);
    return true;
}

// Helper function to prep for updated SD file list
void Menu::prep_for_sd_update(void) {
    prep(_files_menu);
    prep(_firmware_menu);
    prep(_config_menu);
}

// Store path and filename of most recent file on SD card
void Menu::set_recent_file(char *path, bool from_upload) {
    if (from_upload) {
        _recent_file_path = path;
        _recent_file_name = strrchr(path, '/') + 1;
        _recent_file_is_new_upload = true;
        log_info("Recent path set to new upload " << path);
    } else if (!_recent_file_is_new_upload) { // only allow setting from SD refresh if we haven't had a new upload
        _recent_file_path = path;
        _recent_file_name = strrchr(path, '/') + 1;
        log_info("Recent path set to " << path);
    } else {
        log_info("Recent file not from upload denied");
    }
}

// Store path and filename of file we're running
void Menu::set_completed_file(const char *path) {
    _completed_file_path = path;
    _completed_file_name = strrchr(path, '/') + 1;
    log_info("Completed path set to " << path);
}
void Menu::set_completed_file_from_recent() {
    _completed_file_path = _recent_file_path;
    _completed_file_name = _recent_file_name;
}

// Builds the menu system
void Menu::build(void) {
    // Main Menu
    add_entry(_main_menu, _files_menu, NULL, "Browse Files");
    add_entry(_main_menu, NULL, NULL, "Home");

    add_entry(_main_menu, _settings_menu, NULL, "Settings");
    // add_entry(_main_menu, _run_menu, NULL, "Run Files");

    // // Run Menu // not currently used
    // add_entry(_run_menu, NULL, NULL, "< Back");
    // add_entry(_run_menu, NULL, NULL, "Run Latest");
    // add_entry(_run_menu, _files_menu, NULL, "Browse SD");

    // Jogging Menu
    add_entry(_jogging_menu, NULL, NULL, "< Back");
    add_entry(_jogging_menu, NULL, NULL, "Jog X");
    add_entry(_jogging_menu, NULL, NULL, "Jog Y");
    add_entry(_jogging_menu, NULL, NULL, "Jog Z");

    // Files Menu
    add_entry(_files_menu, NULL, NULL, "< Back");

    // Settings Menu
    add_entry(_settings_menu, NULL, NULL, "< Back");
    //add_entry(_settings_menu, NULL, NULL, "Update");  // WebUI already includes OTA functionality
    add_entry(_settings_menu, _version_menu, NULL, "Version");
    add_entry(_settings_menu, _confirm_menu, NULL, "Reset Factory Settings");
    add_entry(_settings_menu, _jogging_menu, NULL, "Jogging");
//    add_entry(_settings_menu, NULL, NULL, "WiFi Info");  // off for initial public release
/*    if( WebUI::wifi_mode->get() != 0 ) { //WebUI::WiFiStartupMode::WiFiOff // temp disabled due to plotting bug
        add_entry(_settings_menu, _jogging_menu, NULL, "Turn WiFi OFF");
    } else {
        add_entry(_settings_menu, _jogging_menu, NULL, "Turn WiFi ON");
    } */
    if (strncmp(config->_name.c_str(), "EggBot", 24) == 0) {
        // special Z-calib command for EggBot, used to assemble servo arm
        add_entry(_settings_menu, NULL, NULL, "Z Calibration Position");
    }
//    add_entry(_settings_menu, _jogging_menu, NULL, "Draw Bounds");
//    add_entry(_settings_menu, NULL, NULL, "TEST");
//    add_entry(_settings_menu, NULL, NULL, "TEST2");

    add_entry(_settings_menu, _config_menu, NULL, "Update Config File");
    add_entry(_config_menu, NULL, NULL, "< Back");
    add_entry(_settings_menu, _firmware_menu, NULL, "Update Firmware");
    add_entry(_firmware_menu, NULL, NULL, "< Back");

    // confirmation for factory reset
    add_entry(_confirm_menu, NULL, NULL, "Cancel Factory Reset");
    add_entry(_confirm_menu, NULL, NULL, "Confirm Factory Reset");
    
    // Version Menu
    char bantam_ver_str[LIST_NAME_MAX_STR] = {"FW Version: "};
    strncat(bantam_ver_str, git_info_short, LIST_NAME_MAX_STR - 13);
    char fluidnc_ver_str[LIST_NAME_MAX_STR] = {"FluidNC: "};
    strncat(fluidnc_ver_str, fluidnc_version, LIST_NAME_MAX_STR - 10);
    char config_ver_str[LIST_NAME_MAX_STR] = {"Config: "};
    strncat(config_ver_str, config->_meta.c_str(), LIST_NAME_MAX_STR - 9);
    char machine_name_str[LIST_NAME_MAX_STR] = {"Machine: "};
    strncat(machine_name_str, config->_name.c_str(), LIST_NAME_MAX_STR - 10);
    char board_name_str[LIST_NAME_MAX_STR] = {"Board: "};
    strncat(board_name_str, config->_board.c_str(), LIST_NAME_MAX_STR - 8);

    add_entry(_version_menu, NULL, NULL, "< Back");
    add_entry(_version_menu, NULL, NULL, bantam_ver_str);
    add_entry(_version_menu, NULL, NULL, fluidnc_ver_str);
    add_entry(_version_menu, NULL, NULL, config_ver_str);
    add_entry(_version_menu, NULL, NULL, machine_name_str);
    add_entry(_version_menu, NULL, NULL, board_name_str);

    // Post-run menu
    add_entry(_postrun_menu, NULL, NULL, "< Back");
    add_entry(_postrun_menu, NULL, NULL, "Run Again"); // special handling to run just-finished file
}

// Updates the current menu selection
void Menu::update_selection(int max_active_entries, int enc_diff) {

    ListNodeType *entry = _current_menu->head;  // Start at the top of the active menu
    ListNodeType *active_tail;

    // Lock out scrolling during operation
    if (sys.state != State::Idle) {
        return;
    }
    
    while (entry) {
        
        // Found selected entry and we have scrolled
        if (entry->selected && (enc_diff != 0)) {

            // Adjust menu selection and active window as needed
            if (enc_diff > 0 && entry->next != NULL) {        // Forwards until hit tail
                entry->next->selected = true;
                entry->selected = false;
                active_tail = get_active_tail(_current_menu, max_active_entries);
                if (active_tail->next && active_tail->next->selected) {  // Shift the window once scroll past max entries
                    _current_menu->active_head = _current_menu->active_head->next;
                }

            } else if (enc_diff < 0 && entry->prev != NULL) { // Backwards until hit head
                entry->prev->selected = true;
                entry->selected = false;
                if (_current_menu->active_head->prev && _current_menu->active_head->prev->selected) {  // Shift the window once scroll past max entries
                    _current_menu->active_head = _current_menu->active_head->prev;
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

// Returns true if the menu occupies the full width of the screen
bool Menu::is_full_width() {
//    return (_current_menu == _files_menu || _current_menu == _rss_menu || 
//            _current_menu == _settings_menu ||_current_menu == _version_menu);
    //return !(_current_menu == _main_menu || _current_menu == _jogging_menu);
    return !(_current_menu == _jogging_menu); // only show DRO alongside jogging at this point
}
