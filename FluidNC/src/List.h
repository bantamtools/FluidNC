#pragma once

#include <cstddef>  // size_t

#include "Config.h"

// Legacy defines kept for compatibility with stack-allocated buffers
#define LIST_NAME_MAX_STR   40
#define LIST_NAME_MAX_PATH  255

// BACK_LABEL is used both as a menu display string and as a strcmp
// key (see Menu.cpp, Protocol.cpp). Centralizing the definition
// ensures all call sites compare identical bytes.
// The ◀ character is U+25C0 BLACK LEFT-POINTING TRIANGLE; rendering
// on the OLED is handled by font_table_lookup in OledTextFit.cpp, which
// maps it to font slot 0x81.
#define BACK_LABEL "◀ Back"

typedef struct ListNodeType
{
    // List neighbor attributes
    struct ListNodeType *prev;
    struct ListNodeType *next;

    // Submenu attributes
    struct ListType *child;

    // List entry characteristics - optimized for memory efficiency
    // IMPORTANT: For file entries, display_name points into path string (after last '/'), NOT separately allocated
    // For static menu entries (path=NULL), display_name is separately allocated
    // Both become invalid when path is freed - always null both together in cleanup
    char *display_name;  // Points into path string for files, or separately allocated for static entries
    char *path;          // Separately allocated full path for files, NULL for static entries
    bool selected;
    bool updated; // optional updated flag (used for RSS updates, etc)

} ListNodeType;

typedef struct ListType {
    struct ListType *parent;
    struct ListNodeType *head;
    struct ListNodeType *active_head;
    char title[80];  // Menu/folder title for display
} ListType;

class List {

protected:

    void init(ListType *list, ListType *parent);
    // extra_path_capacity: extra bytes reserved at the end of the path
    // allocation, only used when path != NULL. Lets the caller pre-reserve
    // slack so that an in-place rename (e.g., adding a 3-byte completion
    // prefix in ) can shift the basename without reallocating.
    bool add_entry(ListType *list, ListType *sublist, const char *path, const char *display_name, bool updated = false, size_t extra_path_capacity = 0);
    void remove_entries(ListType *list);
    void remove_entries_recursive(ListType *list);
    void prep(ListType *list, bool add_back_btn = true);

public:
   
    List();
    ~List();
};
