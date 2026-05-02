#pragma once

#include "List.h"

extern const char* git_info_short;

class Menu : public List {

private:

    ListType *_main_menu, *_files_menu, *_jogging_menu, *_rss_menu, *_settings_menu, *_version_menu, *_run_menu, *_postrun_menu, *_current_menu, *_firmware_menu, *_config_menu, *_confirm_menu, *_homing_choice_menu, *_wifi_info_menu, *_next_file_ordering_menu;
    ListType* _saved_directory_menu;  // Pointer to directory where file was selected
    std::string _recent_file_path;
    std::string _recent_file_name;
    bool _recent_file_is_new_upload;
    std::string _completed_file_path;
    std::string _completed_file_name;
    bool _last_file_succeeded = true;

    struct ListNodeType *get_active_tail(ListType *menu, int max_active_entries);
    void build();
    void build_settings_menu();
    void build_next_file_ordering_menu();

public:

    Menu();
    ~Menu();

    bool is_files_menu();
    bool is_rss_menu();
    bool is_home_menu();
    bool is_run_menu();
    bool is_settings_menu();
    bool is_jogging_menu();
    bool is_version_menu();
    bool is_postrun_menu();
    bool is_firmware_menu();
    bool is_config_menu();
    bool is_confirm_menu();
    bool is_homing_choice_menu();
    bool is_wifi_info_menu();
    void rebuild_wifi_status();
    bool should_clear_on_entry(ListType* menu);
    void connect_rss_feed(ListType *feed);
    void print_current_menu();

    struct ListNodeType *get_active_head();
    struct ListNodeType *get_selected();
    void enter_submenu();
    void exit_submenu();
    ListType* add_directory(char *path, bool isBin = false, bool isCfg = false);
    bool add_sd_file(char *path, bool isBin = false, bool isCfg = false); // return whether we actually added it (hidden/trash files discarded)
    void prep_for_sd_update();
    void finish_sd_update();

    // : rename a single file menu entry in place after an on-disk
    // rename. Walks the file tree to find the entry whose path matches
    // old_path, then shifts the basename within the existing buffer
    // (the buffer was sized at scan time with extra slack for the
    // completion prefix). Returns true if found and updated.
    // Best-effort — if the entry isn't found, caller logs and proceeds.
    bool rename_sd_file_entry(const char *old_path, const char *new_path);

    // Remove a single file entry from the cached menu by exact path
    // match. Returns true if found and removed; false otherwise.
    // Walks recursively into submenus. Empty parent submenus are left
    // in place (cosmetic). Path is base_path-relative form (e.g.,
    // "foo.gcode" or "sub/bar.gcode") — callers strip the "/sd/"
    // prefix before invoking. See SDMenuEvents.h.
    bool remove_sd_file_entry(const char *path);

    // Remove every cached file entry whose path equals dir_prefix or
    // starts with dir_prefix + "/". Returns count removed. Used for
    // recursive directory delete. Empty parent submenus left in place.
    // dir_prefix is base_path-relative.
    int remove_sd_subtree(const char *dir_prefix);

    void set_recent_file(char *path, bool from_upload = false);
    std::string get_recent_file_path() { return _recent_file_path; }
    std::string get_recent_file_name() { return _recent_file_name; }
    void set_completed_file(const char *path);
    std::string get_completed_file_path() { return _completed_file_path; }
    std::string get_completed_file_name() { return _completed_file_name; }
    void set_completed_file_from_recent();
    void set_last_file_succeeded(bool success) { _last_file_succeeded = success; }
    bool get_last_file_succeeded() { return _last_file_succeeded; }
    void return_to_run_menu();
    void go_to_postrun_menu();
    void go_to_files_menu();
    void go_to_homing_choice_menu();
    void save_current_directory();
    void go_to_saved_directory();
    bool is_descendant_of(ListType* menu, ListType* ancestor);
    bool is_in_files_hierarchy();
    void update_selection(int max_active_entries, int enc_diff);
    bool is_full_width();
    void rebuild();
    void rebuild_settings_menu();

    // : persist the new ordering and update the indicator
    // (leading "✓ " vs "  ") on each entry of _next_file_ordering_menu
    // in place. Triggers a menu-only refresh_display so the user sees
    // the ✓ jump to the just-tapped entry without a full menu rebuild.
    // Index 0..3 maps to Oldest / Newest / A_to_Z / Z_to_A.
    void set_next_file_ordering_index(int index);

    ListType* firmware_menu() { return _firmware_menu; };
    ListType* config_menu() { return _config_menu; };
    ListType* files_menu() { return _files_menu; };
    const char* get_current_menu_title();
};
