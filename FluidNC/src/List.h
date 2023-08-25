#pragma once

#include "Config.h"

#define LIST_NAME_MAX_STR   40
#define LIST_NAME_MAX_PATH  255

typedef struct ListNodeType
{
    // List neighbor attributes
    struct ListNodeType *prev;
    struct ListNodeType *next;

    // Submenu attributes
    struct ListType *child;

    // List entry characteristics
    char display_name[LIST_NAME_MAX_STR];
    char path[LIST_NAME_MAX_PATH];
    bool selected;
    bool updated; // optional updated flag (used for RSS updates, etc)

} ListNodeType;

typedef struct ListType {
    struct ListType *parent;
    struct ListNodeType *head;
    struct ListNodeType *active_head;
} ListType;

class List {

protected:

    void init(ListType *list, ListType *parent);
    void add_entry(ListType *list, ListType *sublist, const char *path, const char *display_name, bool updated = false);
    void remove_entries(ListType *list);
    void prep(ListType *list, bool add_back_btn = true);

public:
   
    List();
    ~List();
};
