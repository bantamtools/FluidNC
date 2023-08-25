#include "Menu.h"
#include "Machine/MachineConfig.h"

// Constructor
Menu::Menu() {

    // Allocate memory for the menus
    _main_menu = new struct ListType;
    _files_menu = new struct ListType;
    _jogging_menu = new struct ListType;
    _rss_menu = new struct ListType;
    _settings_menu = new struct ListType;
    _version_menu = new struct ListType;

    // Initialize the menus
    init(_main_menu, NULL);
    init(_files_menu, _main_menu);
    init(_jogging_menu, _main_menu);
    init(_rss_menu, _main_menu);
    init(_settings_menu, _main_menu);
    init(_version_menu, _settings_menu);

    // Set main menu as current
    _current_menu = _main_menu;

    // Build the initial menu
    build();
}

// Destructor
Menu::~Menu() {

    // Set main menu as none
    _current_menu = nullptr;

    // Remove all the menu nodes
    remove(_version_menu);
    remove(_settings_menu);
    remove(_rss_menu);
    remove(_jogging_menu);
    remove(_files_menu);
    remove(_main_menu);

    // Deallocate memory for the menus
    delete(_version_menu);
    delete(_settings_menu);
    delete(_rss_menu);
    delete(_jogging_menu);
    delete(_files_menu);
    delete(_main_menu);
}

// Returns true if the current menu is the files menu
 bool Menu::is_files_menu(void) {
    return (_current_menu == _files_menu);
}

// Returns true if the current menu is the RSS menu
 bool Menu::is_rss_menu(void) {
    return (_current_menu == _rss_menu);
}

// Returns the RSS menu for JSON settings
struct ListNodeType *Menu::get_rss_menu() {
    return _rss_menu->head;   
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

// Helper function to add SD file to files menu
void Menu::add_sd_file(char *path) {

    // Extract the display name from the full path
    char *filename = strrchr(path, '/') + 1;
        
    // Initialize the files menu and attach nodes
    add(_files_menu, NULL, path, filename);
}

// Helper function to add RSS link to RSS menu
void Menu::add_rss_link(const char *link, const char *title, bool is_updated) {

    // Add item to menu
    add(_rss_menu, NULL, link, title, is_updated);
}

// Helper function to prep for updated SD file list
void Menu::prep_for_sd_update(void) {
    prep(_files_menu);
}

// Helper function to prep for updated RSS feed
void Menu::prep_for_rss_update(void) {
    prep(_rss_menu);
}

// Builds the menu system
void Menu::build(void) {

    // Main Menu
    add(_main_menu, NULL, NULL, "Home");
    add(_main_menu, _jogging_menu, NULL, "Jogging");
    add(_main_menu, _files_menu, NULL, "Run from SD");
    add(_main_menu, _rss_menu, NULL, "RSS Feed");
    add(_main_menu, _settings_menu, NULL, "Settings");

    // Jogging Menu
    add(_jogging_menu, NULL, NULL, "< Back");
    add(_jogging_menu, NULL, NULL, "Jog X");
    add(_jogging_menu, NULL, NULL, "Jog Y");
    add(_jogging_menu, NULL, NULL, "Jog Z");

    // Files Menu
    add(_files_menu, NULL, NULL, "< Back");

    // RSS Menu
    add(_rss_menu, NULL, NULL, "< Back");  // Prevents fault if invalid RSS url

    // Settings Menu
    add(_settings_menu, NULL, NULL, "< Back");
    //add(_settings_menu, NULL, NULL, "Update");  // WebUI already includes OTA functionality
    add(_settings_menu, _version_menu, NULL, "Version");

    // Version Menu
    char bantam_ver_str[LIST_NAME_MAX_STR] = {"Version: "};
    strncat(bantam_ver_str, git_info_short, LIST_NAME_MAX_STR - 10);
    char fluidnc_ver_str[LIST_NAME_MAX_STR] = {"FluidNC: "};
    strncat(fluidnc_ver_str, fluidnc_version, LIST_NAME_MAX_STR - 10);

    add(_version_menu, NULL, NULL, "< Back");
    add(_version_menu, NULL, NULL, bantam_ver_str);
    add(_version_menu, NULL, NULL, fluidnc_ver_str);
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
    return (_current_menu == _files_menu || _current_menu == _rss_menu || _current_menu == _version_menu);
}
