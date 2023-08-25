#pragma once

#include "Config.h"

#define MENU_NAME_MAX_STR   40
#define MENU_NAME_MAX_PATH  255

extern const char* git_info_short;
extern const char* fluidnc_version;

typedef struct MenuNodeType
{
    // Menu neighbor attributes
    struct MenuNodeType *prev;
    struct MenuNodeType *next;

    // Submenu attributes
    struct MenuType *child;

    // Menu entry characteristics
    char display_name[MENU_NAME_MAX_STR];
    char path[MENU_NAME_MAX_PATH];
    bool selected;
    bool updated; // optional updated flag (used for RSS updates, etc)

} MenuNodeType;

typedef struct MenuType {
    struct MenuType *parent;
    struct MenuNodeType *head;
    struct MenuNodeType *active_head;
} MenuType;

class Menu {

private:

    MenuType *_main_menu, *_files_menu, *_jogging_menu, *_rss_menu, *_settings_menu, *_version_menu, *_current_menu;

    struct MenuNodeType *get_active_tail(MenuType *menu, int max_active_entries);
    void init(MenuType *menu, MenuType *parent);
    void build();
    void add(MenuType *menu, MenuType *submenu, const char *path, const char *display_name, bool updated = false);
    void remove(MenuType *menu);
    void prep_for_list(MenuType *menu);

public:

    Menu();
    ~Menu();

    bool is_files_menu();
    bool is_rss_menu();
    struct MenuNodeType *get_rss_menu();
    struct MenuNodeType *get_active_head();
    struct MenuNodeType *get_selected();
    void enter_submenu();
    void exit_submenu();
    void add_sd_file(char *path);
    void add_rss_link(const char *link, const char *title, bool is_updated);
    void prep_for_sd_update();
    void prep_for_rss_update();
    bool is_full_width();
    void update_selection(int max_active_entries, int enc_diff);
};
