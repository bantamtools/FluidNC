#pragma once

#include "List.h"

extern const char* git_info_short;
extern const char* fluidnc_version;

class Menu : public List {

private:

    ListType *_main_menu, *_files_menu, *_jogging_menu, *_rss_menu, *_settings_menu, *_version_menu, *_current_menu;

    struct ListNodeType *get_active_tail(ListType *menu, int max_active_entries);
    void build();

public:

    Menu();
    ~Menu();

    bool is_files_menu();
    bool is_rss_menu();
    struct ListNodeType *get_rss_menu();
    struct ListNodeType *get_active_head();
    struct ListNodeType *get_selected();
    void enter_submenu();
    void exit_submenu();
    void add_sd_file(char *path);
    void add_rss_link(const char *link, const char *title, bool is_updated);
    void prep_for_sd_update();
    void prep_for_rss_update();
    void update_selection(int max_active_entries, int enc_diff);
    bool is_full_width();

};
