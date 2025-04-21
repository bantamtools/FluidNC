#pragma once

#include "List.h"

extern const char* git_info_short;
extern const char* fluidnc_version;

class Menu : public List {

private:

    ListType *_main_menu, *_files_menu, *_jogging_menu, *_rss_menu, *_settings_menu, *_version_menu, *_run_menu, *_postrun_menu, *_current_menu, *_firmware_menu, *_config_menu, *_confirm_menu;
    std::string _recent_file_path;
    std::string _recent_file_name;
    bool _recent_file_is_new_upload;
    std::string _completed_file_path;
    std::string _completed_file_name;
    bool _last_file_succeeded = true;

    struct ListNodeType *get_active_tail(ListType *menu, int max_active_entries);
    void build();

public:

    Menu();
    ~Menu();

    bool is_files_menu();
    bool is_rss_menu();
    bool is_home_menu();
    bool is_run_menu();
    bool is_settings_menu();
    bool is_version_menu();
    bool is_postrun_menu();
    bool is_firmware_menu();
    bool is_config_menu();
    bool is_confirm_menu();
    void connect_rss_feed(ListType *feed);
    void print_current_menu();

    struct ListNodeType *get_active_head();
    struct ListNodeType *get_selected();
    void enter_submenu();
    void exit_submenu();
    ListType* add_directory(char *path, bool isBin = false, bool isCfg = false);
    bool add_sd_file(char *path, bool isBin = false, bool isCfg = false); // return whether we actually added it (hidden/trash files discarded)
    void prep_for_sd_update();
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
    void update_selection(int max_active_entries, int enc_diff);
    bool is_full_width();

    ListType* firmware_menu() { return _firmware_menu; };
    ListType* config_menu() { return _config_menu; };
};
