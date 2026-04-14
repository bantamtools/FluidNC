#include "List.h"
#include "Machine/MachineConfig.h"
#include <Esp.h>

// Constructor
List::List() {}

// Destructor
List::~List() {}

// Initializes a list with default settings
void List::init(ListType *list, ListType *parent) {
    
    // Initialize the list to empty with no active window
    list->head = list->active_head = NULL;

    // Set the parent list if one exists
    list->parent = parent;
}

// Adds a node entry to the given list
bool List::add_entry(ListType *list, ListType *sublist, const char *path, const char *display_name, bool updated) {

    // Allocate memory for the new entry
    struct ListNodeType* new_entry = (ListNodeType*)malloc(sizeof(struct ListNodeType));

    // Check for allocation failure
    if (new_entry == NULL) {
        log_error("Failed to allocate memory for menu entry (heap: " << ESP.getFreeHeap() << " bytes)");
        return false;
    }

    // Populate the entry
    new_entry->prev = NULL;
    new_entry->next = NULL;

    new_entry->child = sublist;

    // Memory optimization: display_name points into path string to save allocation overhead
    if (path) {
        // Case 1: File entry with path - allocate path, point display_name into it
        new_entry->path = strdup(path);
        if (new_entry->path == NULL) {
            log_error("Failed to allocate path (heap: " << ESP.getFreeHeap() << " bytes)");
            free(new_entry);
            return false;
        }

        // Point display_name to filename portion within path (no separate allocation)
        if (display_name) {
            char *filename_in_path = strrchr(new_entry->path, '/');
            new_entry->display_name = filename_in_path ? (filename_in_path + 1) : new_entry->path;
        } else {
            new_entry->display_name = NULL;
        }
    } else {
        // Case 2: Static menu entry (e.g., BACK_LABEL) - no path, allocate display_name separately
        new_entry->path = NULL;
        if (display_name) {
            new_entry->display_name = strdup(display_name);
            if (new_entry->display_name == NULL) {
                log_error("Failed to allocate display_name (heap: " << ESP.getFreeHeap() << " bytes)");
                free(new_entry);
                return false;
            }
        } else {
            new_entry->display_name = NULL;
        }
    }

    new_entry->selected = false;
    new_entry->updated = updated;

    // No list entries, insert as the head, set as active window head and select it
    if (list->head == NULL) {
        new_entry->prev = NULL;
        new_entry->selected = true;
        list->head = new_entry;
        list->active_head = new_entry;
        return true;
    }

    // List not empty, traverse to the end to add list item
    struct ListNodeType* temp = list->head;

    // Looking for tail
    while (temp->next != NULL) {
        temp = temp->next;
    }

    // Add list item at the tail
    temp->next = new_entry;
    new_entry->prev = temp;
    return true;
}

// Deletes all nodes in the given list
void List::remove_entries(ListType *list) {

    struct ListNodeType* entry = list->head;

    // Traverse the list until empty, clearing the memory for the nodes
    while(entry) {

        // Attach list head to next node
        list->head = entry->next;

        // Set new node to head unless it's empty
        if (list->head) {
            list->head->prev = NULL;
        }

        // Free the dynamically allocated strings
        // Two cases:
        // 1. If path exists: display_name points into path (no separate free)
        // 2. If path is NULL: display_name is separately allocated (must free)
        if (entry->path) {
            // Case 1: display_name points into path string
            entry->display_name = NULL;  // Invalidate pointer into path BEFORE freeing
            free(entry->path);
            entry->path = NULL;
        } else if (entry->display_name) {
            // Case 2: Static menu entry with separately allocated display_name
            free(entry->display_name);
            entry->display_name = NULL;
        }

        // Free the old node memory
        free(entry);
        entry = NULL;

        // Advance the pointer
        entry = list->head;
    }

    // Mark the head and active head NULL to prevent use-after-free
    list->head = list->active_head = NULL;
}

// Recursively deletes all nodes and child menus in the given list
// Use this for dynamic directory menus only, not static menus
void List::remove_entries_recursive(ListType *list) {

    struct ListNodeType* entry = list->head;

    // Traverse the list until empty, clearing the memory for the nodes
    while(entry) {

        // Attach list head to next node
        list->head = entry->next;

        // Set new node to head unless it's empty
        if (list->head) {
            list->head->prev = NULL;
        }

        // Recursively free child menus (subdirectories)
        if (entry->child) {
            remove_entries_recursive(entry->child);  // Free all entries in child menu
            delete entry->child;                      // Free the child ListType structure
            entry->child = NULL;
        }

        // Free the dynamically allocated strings
        // Two cases:
        // 1. If path exists: display_name points into path (no separate free)
        // 2. If path is NULL: display_name is separately allocated (must free)
        if (entry->path) {
            // Case 1: display_name points into path string
            entry->display_name = NULL;  // Invalidate pointer into path BEFORE freeing
            free(entry->path);
            entry->path = NULL;
        } else if (entry->display_name) {
            // Case 2: Static menu entry with separately allocated display_name
            free(entry->display_name);
            entry->display_name = NULL;
        }

        // Free the old node memory
        free(entry);
        entry = NULL;

        // Advance the pointer
        entry = list->head;
    }

    // Mark the head and active head NULL to prevent use-after-free
    list->head = list->active_head = NULL;
}

// Prepares the given list for an update
void List::prep(ListType *list, bool add_back_btn) {

    // Clear out the menu nodes if they already exist
    if (list->head) {
        remove_entries(list);
    }

    // Add the back button to top of list if requested
    if (add_back_btn) {
        add_entry(list, NULL, NULL, BACK_LABEL);
    }
}
