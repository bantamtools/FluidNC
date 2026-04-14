#pragma once

#include "Config.h"

// Legacy defines kept for compatibility with stack-allocated buffers
#define LIST_NAME_MAX_STR   40
#define LIST_NAME_MAX_PATH  255

// Custom glyph character constants for DejaVu_Sans_10 font on SSD1306 OLED.
// The \xC2 prefix is required: the SSD1306 library's drawString() uses a UTF-8
// decoder that drops raw 0x80-0x9F bytes. \xC2\xNN is valid UTF-8 for U+00NN,
// and the decoder's 0xC2 case passes the second byte through as the font index.
#define GLYPH_BACK_ARROW "\xC2\x81"
#define GLYPH_WIFI       "\xC2\x84"

#define BACK_LABEL GLYPH_BACK_ARROW " Back"

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
    bool add_entry(ListType *list, ListType *sublist, const char *path, const char *display_name, bool updated = false);
    void remove_entries(ListType *list);
    void remove_entries_recursive(ListType *list);
    void prep(ListType *list, bool add_back_btn = true);

public:
   
    List();
    ~List();
};
