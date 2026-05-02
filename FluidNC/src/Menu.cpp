#include "Menu.h"
#include "CompletionMark.h"        // : kPrefixLen, has_completion_prefix
#include "Machine/MachineConfig.h"
#include "SettingsDefinitions.h"   // : completion_marking
#include "WebUI/WifiConfig.h"
#include <Esp.h>
#include <WiFi.h>

// Constructor
Menu::Menu() {

    // Allocate memory for the menus
    _main_menu = new struct ListType;
    _run_menu = new struct ListType;
    _files_menu = new struct ListType;
    _jogging_menu = new struct ListType;
    // _rss_menu is handled by RSSReader
    _rss_menu = nullptr;
    _settings_menu = new struct ListType;
    _version_menu = new struct ListType;
    _postrun_menu = new struct ListType;
    _firmware_menu = new struct ListType;
    _config_menu = new struct ListType;
    _confirm_menu = new struct ListType;
    _homing_choice_menu = new struct ListType;
    _wifi_info_menu = new struct ListType;
    _next_file_ordering_menu = new struct ListType;
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
    init(_homing_choice_menu, _jogging_menu);
    init(_wifi_info_menu, _settings_menu);
    init(_next_file_ordering_menu, _settings_menu);

    // Set main menu as current
    _current_menu = _main_menu;

    // Initialize saved directory pointer
    _saved_directory_menu = nullptr;

    // Initialize menu titles
    strcpy(_main_menu->title, "Main Menu");
    strcpy(_files_menu->title, "Select G-code file");
    strcpy(_settings_menu->title, "Settings");
    strcpy(_jogging_menu->title, "Jog mode");
    strcpy(_version_menu->title, "Version");
    strcpy(_postrun_menu->title, "Plot Complete");
    strcpy(_firmware_menu->title, "Update Firmware");
    strcpy(_config_menu->title, "Update Config");
    strcpy(_confirm_menu->title, "Confirm");
    strcpy(_homing_choice_menu->title, "Machine Not Homed");
    strcpy(_run_menu->title, "Run Menu");
    strcpy(_wifi_info_menu->title, "WiFi Status");
    strcpy(_next_file_ordering_menu->title, "Next File Ordering");

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
    remove_entries_recursive(_files_menu);     // Use recursive for dynamic directories
    remove_entries_recursive(_firmware_menu);  // Use recursive for dynamic directories
    remove_entries_recursive(_config_menu);    // Use recursive for dynamic directories
    remove_entries(_settings_menu);
    remove_entries(_run_menu);
    remove_entries(_main_menu);
    remove_entries(_postrun_menu);
    remove_entries(_confirm_menu);
    remove_entries(_homing_choice_menu);
    remove_entries(_wifi_info_menu);
    remove_entries(_next_file_ordering_menu);

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
    delete(_homing_choice_menu);
    delete(_wifi_info_menu);
    delete(_next_file_ordering_menu);
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
bool Menu::is_jogging_menu(void) {
    return (_current_menu == _jogging_menu);
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
bool Menu::is_homing_choice_menu(void) {
    return (_current_menu == _homing_choice_menu);
}
bool Menu::is_wifi_info_menu(void) {
    return (_current_menu == _wifi_info_menu);
}

bool Menu::should_clear_on_entry(ListType* menu) {
    // Clear screen when entering these menu types to eliminate display artifacts
    return (menu == _jogging_menu ||     // Jogging menu has real-time position updates
            menu == _settings_menu ||    // Settings menu benefits from clean slate
            menu == _files_menu);        // File menu benefits from clean slate
}

// Connects the RSS feed to the menu system
void Menu::connect_rss_feed(ListType *feed) {

    // Hook up RSS menu to feed directly
    _rss_menu = feed;

    // Initialize and connect into menu system
    init(_rss_menu, _settings_menu);
    add_entry(_settings_menu, _rss_menu, NULL, "RSS Feed");
    add_entry(_rss_menu, NULL, NULL, BACK_LABEL);
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

const char* Menu::get_current_menu_title() {
    if (_current_menu && _current_menu->title[0] != '\0') {
        return _current_menu->title;
    }
    // Fallback for any menu without a title
    return "Menu";
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

        // Clear screen for certain menu types to eliminate display artifacts
        if (should_clear_on_entry(_current_menu) && config->_oled) {
            config->_oled->clear();
        }

        // Special handling for jog menu - show initial header
        if (_current_menu == _jogging_menu && config->_oled) {
            config->_oled->showJogHeaderFast(false);  // Show "Jog mode" header
        }

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

void Menu::go_to_homing_choice_menu() {
    _current_menu = _homing_choice_menu;
    if(config->_oled){
        config->_oled->refresh_display();
    }
}

bool Menu::is_descendant_of(ListType* menu, ListType* ancestor) {
    if (!menu || !ancestor) {
        return false;
    }
    ListType* current = menu;
    while (current && current != ancestor) {
        current = current->parent;
    }
    return (current == ancestor);
}

bool Menu::is_in_files_hierarchy() {
    return (_current_menu == _files_menu || is_descendant_of(_current_menu, _files_menu));
}

void Menu::save_current_directory() {
    // Only save if we're in files menu or subdirectory
    if (is_in_files_hierarchy()) {
        _saved_directory_menu = _current_menu;

        // Build path for logging by traversing parent chain
        std::string path = "";
        ListType* menu = _current_menu;
        while (menu && menu != _files_menu) {
            if (path.empty()) {
                path = menu->title;
            } else {
                path = std::string(menu->title) + "/" + path;
            }
            menu = menu->parent;
        }

        log_info("Saved directory: /" << (path.empty() ? "(root)" : path.c_str()));
    }
}

void Menu::go_to_saved_directory() {
    // Validate saved directory is still valid
    if (_saved_directory_menu &&
        _saved_directory_menu->head &&  // Has entries
        is_descendant_of(_saved_directory_menu, _files_menu)) {
        _current_menu = _saved_directory_menu;

        // Reset selection to top of directory (not a specific file)
        if (_current_menu->head) {
            // Clear all selections
            ListNodeType* entry = _current_menu->head;
            while (entry) {
                entry->selected = false;
                entry = entry->next;
            }
            // Select first entry (usually BACK_LABEL)
            _current_menu->head->selected = true;
            _current_menu->active_head = _current_menu->head;
        }

        log_info("Restored to saved directory");
    } else {
        // Fallback to root files menu if available
        if (_files_menu) {
            _current_menu = _files_menu;
            log_info("Restored to files root (saved directory invalid)");
        } else {
            log_error("Cannot restore directory - files menu not available");
        }
        _saved_directory_menu = nullptr;  // Clear invalid pointer
    }

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
        
        // Save the current folder name for potential title use
        char folder_name[80];
        strncpy(folder_name, token, 79);
        folder_name[79] = '\0';

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

            // Check for allocation failure
            if (new_menu == NULL) {
                log_error("Failed to allocate directory menu (heap: " << ESP.getFreeHeap() << " bytes)");
                return current_menu;  // Return current menu instead of NULL to handle gracefully
            }

            init(new_menu, current_menu);
            // Set folder name as title (without trailing slash)
            strcpy(new_menu->title, folder_name);
            // Add a "Back" button at the start of each new submenu
            prep(new_menu);
//            log_info("Adding menu entry for folder: " << token_copy);
            if (!add_entry(current_menu, new_menu, NULL, token_copy)) {
                // Failed to add entry, clean up and return
                remove_entries(new_menu);  // Free the BACK_LABEL entry first
                delete new_menu;
                return current_menu;
            }
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
    if (path_copy == NULL) {
        log_warn("Failed to allocate memory for path copy");
        return false;
    }

    ListType *file_menu = add_directory(path_copy, isBin, isCfg);
    free(path_copy);

    // Check if directory creation succeeded
    if (file_menu == NULL) {
        log_warn("Failed to create directory structure for: " << path);
        return false;
    }

    // Extract the display name from the full path
    char *filename = strrchr(path, '/') + 1;

    // Filter again for trashed/hidden files
    if (strncmp(filename, ".", 1) == 0) { // file starts with '.'
//        log_info("Discarded hidden file: " << path);
        return false;
    }

//    log_info("Adding menu entry for filepath: " << path);
    // Add the file to the correct submenu.
    //
    // : reserve CompletionMark::kPrefixLen bytes of slack at the end of
    // the path allocation so a later mark/unmark can shift the basename in
    // place without reallocating. See Menu::rename_sd_file_entry.
    if (!add_entry(file_menu, NULL, path, filename, /*updated=*/false,
                   /*extra_path_capacity=*/CompletionMark::kPrefixLen)) {
        log_warn("Failed to add file to menu: " << path);
        return false;
    }
//    add_entry(_files_menu, NULL, path, filename);
    return true;
}

// Helper function to prep for updated SD file list
void Menu::prep_for_sd_update(void) {
    uint32_t heap_start = ESP.getFreeHeap();
    float heap_kb_start = heap_start / 1024.0;

#ifdef DEBUG_STACK_USAGE
    const uint32_t STACK_TOTAL_WORDS = 6144; // From ARDUINO_LOOP_STACK_SIZE in main.cpp
    uint32_t stack_words_start = uxTaskGetStackHighWaterMark(NULL);
    float stack_kb_start = (stack_words_start * 4) / 1024.0;
    float stack_total_kb = (STACK_TOTAL_WORDS * 4) / 1024.0;
    uint32_t stack_used_pct = ((STACK_TOTAL_WORDS - stack_words_start) * 100) / STACK_TOTAL_WORDS;
    log_info("SD file list load START - Stack: " << stack_kb_start << " kB free / " << stack_total_kb << " kB total (" << stack_used_pct << "% used) | Heap: " << heap_kb_start << " kB free");
#else
    log_info("SD file list load START - Heap: " << heap_kb_start << " kB free");
#endif

    _saved_directory_menu = nullptr;  // Clear saved directory as menu structure will be rebuilt

    // If currently in any file-browsing menu tree, reset to safe location before destroying dynamic menus
    if (is_in_files_hierarchy() && _current_menu != _files_menu) {
        _current_menu = _files_menu;
    } else if (is_firmware_menu() || is_descendant_of(_current_menu, _firmware_menu)) {
        _current_menu = _firmware_menu;
    } else if (is_config_menu() || is_descendant_of(_current_menu, _config_menu)) {
        _current_menu = _config_menu;
    }

    // Use recursive cleanup for all file-browsing menus to free dynamic directory structures
    if (_files_menu->head) {
        remove_entries_recursive(_files_menu);
    }
    add_entry(_files_menu, NULL, NULL, BACK_LABEL);

    if (_firmware_menu->head) {
        remove_entries_recursive(_firmware_menu);
    }
    add_entry(_firmware_menu, NULL, NULL, BACK_LABEL);

    if (_config_menu->head) {
        remove_entries_recursive(_config_menu);
    }
    add_entry(_config_menu, NULL, NULL, BACK_LABEL);

    uint32_t heap_cleared = ESP.getFreeHeap();
    float heap_kb_cleared = heap_cleared / 1024.0;

#ifdef DEBUG_STACK_USAGE
    uint32_t stack_words_cleared = uxTaskGetStackHighWaterMark(NULL);
    float stack_kb_cleared = (stack_words_cleared * 4) / 1024.0;
    uint32_t stack_used_pct_cleared = ((STACK_TOTAL_WORDS - stack_words_cleared) * 100) / STACK_TOTAL_WORDS;
    log_info("SD file list CLEARED - Stack: " << stack_kb_cleared << " kB free / " << stack_total_kb << " kB total (" << stack_used_pct_cleared << "% used) | Heap: " << heap_kb_cleared << " kB free");
#else
    log_info("SD file list CLEARED - Heap: " << heap_kb_cleared << " kB free");
#endif
}

void Menu::finish_sd_update(void) {
    uint32_t heap_end = ESP.getFreeHeap();
    float heap_kb_end = heap_end / 1024.0;

#ifdef DEBUG_STACK_USAGE
    const uint32_t STACK_TOTAL_WORDS = 6144;
    uint32_t stack_words_end = uxTaskGetStackHighWaterMark(NULL);
    float stack_kb_end = (stack_words_end * 4) / 1024.0;
    float stack_total_kb = (STACK_TOTAL_WORDS * 4) / 1024.0;
    uint32_t stack_used_pct_end = ((STACK_TOTAL_WORDS - stack_words_end) * 100) / STACK_TOTAL_WORDS;
    log_info("SD file list load END - Stack: " << stack_kb_end << " kB free / " << stack_total_kb << " kB total (" << stack_used_pct_end << "% used) | Heap: " << heap_kb_end << " kB free");
#else
    log_info("SD file list load END - Heap: " << heap_kb_end << " kB free");
#endif
}

// : walk a list (recursively into submenus) looking for an entry
// whose path matches old_path. Returns the matching node or nullptr.
static ListNodeType* find_file_entry_recursive(ListType* list, const char* old_path) {
    if (list == nullptr) return nullptr;
    for (ListNodeType* node = list->head; node != nullptr; node = node->next) {
        if (node->path != nullptr && strcmp(node->path, old_path) == 0) {
            return node;
        }
        if (node->child != nullptr) {
            ListNodeType* hit = find_file_entry_recursive(node->child, old_path);
            if (hit != nullptr) return hit;
        }
    }
    return nullptr;
}

bool Menu::rename_sd_file_entry(const char *old_path, const char *new_path) {
    if (old_path == nullptr || new_path == nullptr) return false;
    if (_files_menu == nullptr) return false;

    ListNodeType* node = find_file_entry_recursive(_files_menu, old_path);
    if (node == nullptr) {
        // Entry not in the cached menu — could happen if the file was
        // added via a path that bypassed the SD scan, or if the menu
        // was rebuilt between the rename and this call. Caller logs.
        return false;
    }

    // Buffer-size invariant: each scanned file entry was allocated with
    // kPrefixLen extra bytes of slack (see add_sd_file). That guarantees
    // the buffer can hold either direction of a single mark/unmark
    // operation — mark grows by kPrefixLen, strip shrinks. For any other
    // rename shape (e.g., a future caller passing a much longer name),
    // refuse rather than overflow.
    size_t new_len = strlen(new_path);
    size_t old_len = strlen(node->path);
    if (new_len > old_len + CompletionMark::kPrefixLen) {
        log_warn("rename_sd_file_entry: new path too long for reserved buffer "
                 "(new=" << new_len << ", old=" << old_len << ", slack="
                 << CompletionMark::kPrefixLen << "), refusing in-place update for " << old_path);
        return false;
    }

    // Copy new_path into the existing buffer; recompute display_name to
    // point past the last '/' in the new content.
    memcpy(node->path, new_path, new_len + 1);
    char* slash = strrchr(node->path, '/');
    node->display_name = slash ? (slash + 1) : node->path;

    return true;
}

// Like find_file_entry_recursive but also returns the parent list
// pointer so the node can be spliced out. out_parent is set to the
// ListType whose `head` chain currently contains the matched node.
static ListNodeType* find_file_entry_with_parent(ListType* list,
                                                 const char* path,
                                                 ListType*& out_parent) {
    if (list == nullptr) return nullptr;
    for (ListNodeType* node = list->head; node != nullptr; node = node->next) {
        if (node->path != nullptr && strcmp(node->path, path) == 0) {
            out_parent = list;
            return node;
        }
        if (node->child != nullptr) {
            ListNodeType* hit =
                find_file_entry_with_parent(node->child, path, out_parent);
            if (hit != nullptr) return hit;
        }
    }
    return nullptr;
}

// SD entries can live in any of three root menus: _files_menu
// (gcode/nc/txt), _firmware_menu (.bin), _config_menu (.yaml). The
// populate function clears and rebuilds all three, so incremental
// removal must search all three to match.
static ListNodeType* find_sd_entry_with_parent(ListType* files_root,
                                               ListType* firmware_root,
                                               ListType* config_root,
                                               const char* path,
                                               ListType*& out_parent) {
    ListNodeType* hit =
        find_file_entry_with_parent(files_root, path, out_parent);
    if (hit != nullptr) return hit;
    hit = find_file_entry_with_parent(firmware_root, path, out_parent);
    if (hit != nullptr) return hit;
    return find_file_entry_with_parent(config_root, path, out_parent);
}

// Splice `node` out of `parent`'s doubly-linked list and free it.
// Maintains five invariants (mirrors List::add_entry / remove_entries
// patterns -- a non-empty list always has exactly one node with
// selected==true, anchored by active_head):
//   1. prev/next chain: predecessor's next, successor's prev.
//   2. parent->head: if removing the head, advance to next.
//   3. parent->active_head: if it pointed to the removed node,
//      advance to next (or fall back to head if next is null).
//   4. Selection: if the removed node was selected, transfer
//      selected=true to parent->active_head (the new "current"
//      entry). Without this, get_selected() walks the chain and
//      returns nullptr; protocol_do_enter then derefs nullptr and
//      crashes (EXCVADDR=0x0000000c, the offset of display_name).
//   5. display_name lives inside the path buffer for file entries
//      (see ListNodeType comments), so freeing path also frees
//      display_name -- null both pointers before/together.
static void splice_and_free_node(ListType* parent, ListNodeType* node) {
    ListNodeType* prev = node->prev;
    ListNodeType* next = node->next;
    bool was_selected = node->selected;

    if (parent->head == node) {
        parent->head = next;
    }
    if (parent->active_head == node) {
        parent->active_head = next ? next : parent->head;
    }
    if (prev != nullptr) {
        prev->next = next;
    }
    if (next != nullptr) {
        next->prev = prev;
    }

    // Transfer selection if needed. parent->active_head, if non-null,
    // is the right new "current" entry; if the list is now empty the
    // selection invariant is vacuous.
    if (was_selected && parent->active_head != nullptr) {
        parent->active_head->selected = true;
    }

    if (node->path != nullptr) {
        node->display_name = nullptr;  // points into path; invalidate first
        free(node->path);
        node->path = nullptr;
    }
    free(node);
}

bool Menu::remove_sd_file_entry(const char* path) {
    if (path == nullptr) return false;
    ListType* parent = nullptr;
    ListNodeType* node = find_sd_entry_with_parent(
        _files_menu, _firmware_menu, _config_menu, path, parent);
    if (node == nullptr || parent == nullptr) {
        return false;
    }
    splice_and_free_node(parent, node);
    return true;
}

// True if path equals dir_prefix exactly or begins with dir_prefix
// + "/". Avoids false positives like "sub2" matching prefix "sub".
static bool path_matches_subtree(const char* path,
                                 const char* dir_prefix,
                                 size_t prefix_len) {
    if (strncmp(path, dir_prefix, prefix_len) != 0) return false;
    char tail = path[prefix_len];
    return tail == '\0' || tail == '/';
}

// Walks parent's children removing any node whose path matches the
// subtree. Recurses into submenus first so nested matches are caught
// even if the directory entry itself does not match the prefix.
// Returns count removed at this level and below.
//
// Uses splice_and_free_node so prev/next/active_head invariants are
// maintained -- the doubly-linked list must stay walkable in both
// directions, and Menu's get_selected() walks from
// ListType::active_head, so a stale active_head causes a
// LoadProhibited crash on the next menu interaction.
static int remove_subtree_walk(ListType* list,
                               const char* dir_prefix,
                               size_t prefix_len) {
    if (list == nullptr) return 0;
    int removed = 0;
    ListNodeType* node = list->head;
    while (node != nullptr) {
        ListNodeType* next = node->next;  // capture before potential free
        if (node->child != nullptr) {
            removed += remove_subtree_walk(node->child, dir_prefix, prefix_len);
        }
        bool match = node->path != nullptr &&
                     path_matches_subtree(node->path, dir_prefix, prefix_len);
        if (match) {
            splice_and_free_node(list, node);
            ++removed;
        }
        node = next;
    }
    return removed;
}

int Menu::remove_sd_subtree(const char* dir_prefix) {
    if (dir_prefix == nullptr) return 0;
    size_t prefix_len = strlen(dir_prefix);
    int removed = 0;
    // Walk all three SD-content roots; a recursive directory delete
    // can sweep through gcode/firmware/config files indiscriminately
    // on disk, so the cache walk has to mirror that.
    if (_files_menu)    removed += remove_subtree_walk(_files_menu,    dir_prefix, prefix_len);
    if (_firmware_menu) removed += remove_subtree_walk(_firmware_menu, dir_prefix, prefix_len);
    if (_config_menu)   removed += remove_subtree_walk(_config_menu,   dir_prefix, prefix_len);
    return removed;
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
    // add_entry(_run_menu, NULL, NULL, BACK_LABEL);
    // add_entry(_run_menu, NULL, NULL, "Run Latest");
    // add_entry(_run_menu, _files_menu, NULL, "Browse SD");

    // Jogging Menu
    add_entry(_jogging_menu, NULL, NULL, BACK_LABEL);
    
    // Add jog entries for configured axes (limit to 3 for display)
    if (config && config->_axes && config->_axes->_numberAxis > 0) {
        int jog_entries = 0;
        
        // Define preferred axis order per machine type
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
        
        // Add jog entries in preferred order
        for (int i = 0; i < 3 && jog_entries < 3; i++) {
            int axis_idx = axis_order[i];
            if (axis_idx < config->_axes->_numberAxis && 
                config->_axes->_axis[axis_idx] && 
                config->_axes->_axis[axis_idx]->_motors[0] && 
                config->_axes->_axis[axis_idx]->_motors[0]->isReal()) {
                
                char jog_text[10];
                snprintf(jog_text, sizeof(jog_text), "Jog %c", config->_axes->axisName(axis_idx));
                add_entry(_jogging_menu, NULL, NULL, jog_text);
                jog_entries++;
            }
        }
        
        // If no axes were added, fall back to defaults
        if (jog_entries == 0) {
            add_entry(_jogging_menu, NULL, NULL, "Jog X");
            add_entry(_jogging_menu, NULL, NULL, "Jog Y");
            add_entry(_jogging_menu, NULL, NULL, "Jog Z");
        }
    } else {
        // Fallback to default axes if config not ready
        add_entry(_jogging_menu, NULL, NULL, "Jog X");
        add_entry(_jogging_menu, NULL, NULL, "Jog Y");
        add_entry(_jogging_menu, NULL, NULL, "Jog Z");
    }

    // Files Menu
    add_entry(_files_menu, NULL, NULL, BACK_LABEL);

    // Settings Menu
    build_settings_menu();

    // : pre-populate the Next File Ordering submenu so it's
    // ready when the user enters it.
    build_next_file_ordering_menu();

    // Back buttons for settings submenus (only added once during build)
    add_entry(_config_menu, NULL, NULL, BACK_LABEL);
    add_entry(_firmware_menu, NULL, NULL, BACK_LABEL);

    // confirmation for factory reset
    add_entry(_confirm_menu, NULL, NULL, "Cancel Factory Reset");
    add_entry(_confirm_menu, NULL, NULL, "Confirm Factory Reset");
    
    // homing choice menu entries
    add_entry(_homing_choice_menu, NULL, NULL, BACK_LABEL);
    add_entry(_homing_choice_menu, NULL, NULL, "Run Homing");
    
    // Version Menu
    char bantam_ver_str[LIST_NAME_MAX_STR] = {"FW Version: "};
    strncat(bantam_ver_str, git_info_short, LIST_NAME_MAX_STR - 13);
    char config_ver_str[LIST_NAME_MAX_STR] = {"Config: "};
    strncat(config_ver_str, config->_meta.c_str(), LIST_NAME_MAX_STR - 9);
    char machine_name_str[LIST_NAME_MAX_STR];
    strncpy(machine_name_str, config->_name.c_str(), LIST_NAME_MAX_STR - 1);
    machine_name_str[LIST_NAME_MAX_STR - 1] = '\0';
    char board_name_str[LIST_NAME_MAX_STR];
    strncpy(board_name_str, config->_board.c_str(), LIST_NAME_MAX_STR - 1);
    board_name_str[LIST_NAME_MAX_STR - 1] = '\0';
    char wifi_mode_str[LIST_NAME_MAX_STR];
    switch (config->_wifiMode) {
        case -1: snprintf(wifi_mode_str, sizeof(wifi_mode_str), "WiFi: -1 (user setting)"); break;
        case 0:  snprintf(wifi_mode_str, sizeof(wifi_mode_str), "WiFi: 0 (Off)"); break;
        case 1:  snprintf(wifi_mode_str, sizeof(wifi_mode_str), "WiFi: 1 (STA)"); break;
        case 2:  snprintf(wifi_mode_str, sizeof(wifi_mode_str), "WiFi: 2 (AP)"); break;
        case 3:  snprintf(wifi_mode_str, sizeof(wifi_mode_str), "WiFi: 3 (STA>AP)"); break;
        default: snprintf(wifi_mode_str, sizeof(wifi_mode_str), "WiFi: %d", config->_wifiMode); break;
    }

    add_entry(_version_menu, NULL, NULL, BACK_LABEL);
    add_entry(_version_menu, NULL, NULL, bantam_ver_str);
    add_entry(_version_menu, NULL, NULL, machine_name_str);
    add_entry(_version_menu, NULL, NULL, config_ver_str);
    add_entry(_version_menu, NULL, NULL, board_name_str);
    add_entry(_version_menu, NULL, NULL, wifi_mode_str);

    // Post-run menu
    add_entry(_postrun_menu, NULL, NULL, BACK_LABEL);
    add_entry(_postrun_menu, NULL, NULL, "Run Again"); // special handling to run just-finished file
}

// Populates _settings_menu entries only. Does not touch child submenu
// contents (file lists, confirm entries, etc). Safe to call after
// remove_entries(_settings_menu) for a targeted refresh.
void Menu::build_settings_menu() {
    add_entry(_settings_menu, NULL, NULL, BACK_LABEL);
    add_entry(_settings_menu, _version_menu, NULL, "Version");
    add_entry(_settings_menu, _jogging_menu, NULL, "Jog mode");
    // WiFi Status submenu — before toggle so user sees status first
    if (WebUI::wifi_config.isOn()) {
        add_entry(_settings_menu, _wifi_info_menu, NULL, "WiFi Status");
        rebuild_wifi_status();
    }
    // WiFi toggle menu item
    // - Hidden when config enforces WiFi off (wifi_mode: 0)
    // - When config defers (wifi_mode: -1): user has full on/off control
    // - When config enforces a mode (1/2/3): user can turn off temporarily
    if (config->_wifiMode == -1) {
        // Read live radio state, not the persisted wifi_mode setting:
        // $Radio/State=OFF turns the radio off without touching the
        // setting, so wifi_mode->get() stays non-zero and the label
        // would lie. wifi_config.isOn() matches the source of truth
        // used everywhere else (and by the _wifiMode>0 branch below).
        if (WebUI::wifi_config.isOn()) {
            add_entry(_settings_menu, NULL, NULL, "Turn WiFi OFF");
        } else {
            add_entry(_settings_menu, NULL, NULL, "Turn WiFi ON");
        }
    } else if (config->_wifiMode > 0) {
        if (WebUI::wifi_config.isOn()) {
            add_entry(_settings_menu, NULL, NULL, "Turn WiFi OFF");
        } else {
            add_entry(_settings_menu, NULL, NULL, "WiFi: Reboot to enable");
        }
    }
    // : completed-file marking toggle. Label reflects current state.
    if (completion_marking != nullptr && completion_marking->get() != 0) {
        add_entry(_settings_menu, NULL, NULL, "\xE2\x9C\x93" " Completed files: ON");
    } else {
        add_entry(_settings_menu, NULL, NULL, "\xE2\x9C\x93" " Completed files: OFF");
    }
    // : navigate into the Next File Ordering submenu. The double-
    // quotes around "Next File" signal that "Next File" is a feature
    // name (the upcoming Plot Next feature), making the entry less
    // ambiguous than plain "Next File Ordering".
    add_entry(_settings_menu, _next_file_ordering_menu, NULL,
              "\"Next File\" Ordering");
    if (config->getMachineType() == Machine::MachineType::EggBot ||
        config->getMachineType() == Machine::MachineType::WaterColorBot) {
        add_entry(_settings_menu, NULL, NULL, "Z Calibration Position");
    }
    add_entry(_settings_menu, _config_menu, NULL, "Update Config File");
    add_entry(_settings_menu, _firmware_menu, NULL, "Update Firmware");
    add_entry(_settings_menu, _confirm_menu, NULL, "Reset Factory Settings");
    // Re-link RSS feed if it was previously connected
    if (_rss_menu) {
        add_entry(_settings_menu, _rss_menu, NULL, "RSS Feed");
    }
}

// : populate the Next File Ordering submenu. The leading "✓ "
// (U+2713) on the chosen entry is the indicator; non-chosen entries
// get a 3-space pad to keep labels x-aligned with the chosen form.
//
// 3 spaces (not 2) because the OLED font is not fixed-width: the
// ✓ glyph at font slot 0x83 renders visibly wider than a single
// space, so two-space padding leaves a small but noticeable gap
// at the start of unchosen entries. Three spaces matches the
// chosen form's left edge on the actual hardware.
//
// Built at boot and from the public set_next_file_ordering_index()
// helper so a fresh rebuild reflects the current setting value.
void Menu::build_next_file_ordering_menu() {
    add_entry(_next_file_ordering_menu, NULL, NULL, BACK_LABEL);
    int8_t cur = (next_file_ordering != nullptr) ? next_file_ordering->get() : 0;

    add_entry(_next_file_ordering_menu, NULL, NULL,
              cur == 0 ? "\xE2\x9C\x93 Oldest" : "   Oldest");
    add_entry(_next_file_ordering_menu, NULL, NULL,
              cur == 1 ? "\xE2\x9C\x93 Newest" : "   Newest");
    add_entry(_next_file_ordering_menu, NULL, NULL,
              cur == 2 ? "\xE2\x9C\x93 A \xE2\x86\x92 Z"
                       : "   A \xE2\x86\x92 Z");
    add_entry(_next_file_ordering_menu, NULL, NULL,
              cur == 3 ? "\xE2\x9C\x93 Z \xE2\x86\x92 A"
                       : "   Z \xE2\x86\x92 A");
}

// : update the chosen-entry indicator in place after a tap. The
// in-place rewrite avoids the full rebuild_*-then-redraw flicker that
// would otherwise reset the user's selection cursor (see 's PR
//  for the same primitive applied to the toggle entry).
void Menu::set_next_file_ordering_index(int index) {
    if (index < 0 || index > 3) {
        // Defensive: callers should pass 0..3 only. Out-of-range is
        // a programming error; log and ignore.
        log_warn("set_next_file_ordering_index: out-of-range " << index);
        return;
    }
    if (next_file_ordering == nullptr) {
        log_warn("set_next_file_ordering_index: setting not initialized");
        return;
    }

    // 1. Persist the new value via EnumSetting using the WebUI string.
    static const char* const kWireStrings[4] = {
        "Oldest", "Newest", "A_to_Z", "Z_to_A",
    };
    next_file_ordering->setStringValue(const_cast<char*>(kWireStrings[index]));

    // 2. Walk the submenu's entries and rewrite each entry's
    //    display_name in place. Skip the BACK_LABEL entry (head).
    static const char* const kChosenLabels[4] = {
        "\xE2\x9C\x93 Oldest",
        "\xE2\x9C\x93 Newest",
        "\xE2\x9C\x93 A \xE2\x86\x92 Z",
        "\xE2\x9C\x93 Z \xE2\x86\x92 A",
    };
    // 3-space pad matches the chosen form's left edge on hardware;
    // see comment on build_next_file_ordering_menu() for why three
    // (and not two) spaces. Must stay in sync with the build helper
    // and with the strcmp targets in Protocol.cpp's settings-menu
    // dispatch.
    static const char* const kPadLabels[4] = {
        "   Oldest",
        "   Newest",
        "   A \xE2\x86\x92 Z",
        "   Z \xE2\x86\x92 A",
    };

    ListNodeType* node = _next_file_ordering_menu->head;
    if (node == nullptr) return;
    node = node->next;  // skip BACK
    for (int i = 0; i < 4 && node != nullptr; i++, node = node->next) {
        const char* new_label = (i == index) ? kChosenLabels[i] : kPadLabels[i];
        free(node->display_name);
        node->display_name = strdup(new_label);
    }

    // 3. Repaint the menu only. Selection stays where the user
    //    tapped; no encoder-jump-to-top. refresh_display(true) drops
    //    any pending encoder rotation at entry (see ), so press-
    //    induced wobble pulses don't get applied as cursor motion.
    if (config && config->_oled) {
        config->_oled->refresh_display(true);
    }
}

// Rebuilds only the settings menu entries, preserving file lists
// and all other menu state. Uses non-recursive remove_entries()
// which frees link nodes but not child submenus.
void Menu::rebuild_settings_menu() {
    remove_entries(_settings_menu);
    build_settings_menu();
}

void Menu::rebuild_wifi_status() {
    remove_entries(_wifi_info_menu);
    add_entry(_wifi_info_menu, NULL, NULL, BACK_LABEL);

    // Radio off (explicit $Radio/State=OFF, or a failed bring-up that fell
    // through to the wifi_off path). Render the off state explicitly rather
    // than early-returning, which would leave whatever the menu rendered
    // last (often a stale "Connected" body) on screen. See issue.
    if (!WebUI::wifi_config.isOn()) {
        strcpy(_wifi_info_menu->title, "WiFi: Disabled");
        add_entry(_wifi_info_menu, NULL, NULL, "Radio is off");
        return;
    }

    char buf[LIST_NAME_MAX_STR];
    bool sta_connected = WebUI::WiFiConfig::sta_got_ip();
    wifi_mode_t wm = WiFi.getMode();
    int configured_mode = WebUI::wifi_mode->get();

    if (sta_connected) {
        // STA connected (either pure STA or STA>AP that succeeded)
        strcpy(_wifi_info_menu->title, "WiFi Status: Connected");
        add_entry(_wifi_info_menu, NULL, NULL, "Connected to network:");
        add_entry(_wifi_info_menu, NULL, NULL, WiFi.SSID().c_str());
        snprintf(buf, sizeof(buf), "IP: %s", IP_string(WiFi.localIP()).c_str());
        add_entry(_wifi_info_menu, NULL, NULL, buf);
    } else if (wm == WIFI_MODE_AP || wm == WIFI_MODE_APSTA) {
        // Running as AP — intentional or fallback?
        strcpy(_wifi_info_menu->title, "WiFi Status: Hotspot");
        if (configured_mode == WebUI::WiFiFallback) {
            add_entry(_wifi_info_menu, NULL, NULL, "Hotspot (fallback mode):");
        } else {
            add_entry(_wifi_info_menu, NULL, NULL, "WiFi Hotspot:");
        }
        add_entry(_wifi_info_menu, NULL, NULL, WebUI::wifi_ap_ssid->get());
        snprintf(buf, sizeof(buf), "Pass: %s", WebUI::wifi_ap_password->get());
        add_entry(_wifi_info_menu, NULL, NULL, buf);
        snprintf(buf, sizeof(buf), "IP: %s", IP_string(WiFi.softAPIP()).c_str());
        add_entry(_wifi_info_menu, NULL, NULL, buf);
    } else {
        // STA mode, not connected
        strcpy(_wifi_info_menu->title, "WiFi: Not connected");
        add_entry(_wifi_info_menu, NULL, NULL, "Not connected");
        add_entry(_wifi_info_menu, NULL, NULL, WebUI::wifi_sta_ssid->get());
        add_entry(_wifi_info_menu, NULL, NULL, "IP: <not connected>");
    }

    snprintf(buf, sizeof(buf), "Host: %s", WebUI::wifi_hostname->get());
    add_entry(_wifi_info_menu, NULL, NULL, buf);
}

// Rebuilds the menu system (e.g., after machine type is determined)
void Menu::rebuild(void) {
    _saved_directory_menu = nullptr;  // Clear saved directory as menu structure will be rebuilt

    // If currently in any menu tree with dynamic directories, reset to main menu before destroying them
    if (is_in_files_hierarchy() ||
        is_firmware_menu() || is_descendant_of(_current_menu, _firmware_menu) ||
        is_config_menu() || is_descendant_of(_current_menu, _config_menu)) {
        _current_menu = _main_menu;
    }

    // Clear all existing menu entries first
    remove_entries(_main_menu);
    remove_entries_recursive(_files_menu);     // Use recursive to free dynamic directory menus
    remove_entries(_jogging_menu);
    remove_entries(_settings_menu);
    remove_entries(_version_menu);
    remove_entries(_run_menu);
    remove_entries(_postrun_menu);
    remove_entries_recursive(_firmware_menu);  // Use recursive to free dynamic directory menus
    remove_entries_recursive(_config_menu);    // Use recursive to free dynamic directory menus
    remove_entries(_confirm_menu);
    remove_entries(_homing_choice_menu);
    remove_entries(_wifi_info_menu);
    remove_entries(_next_file_ordering_menu);  // 

    // Rebuild the menu structure with current config
    build();
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
