#include "List.h"
#include "Machine/MachineConfig.h"

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
void List::add(ListType *list, ListType *sublist, const char *path, const char *display_name, bool updated) {

    // Allocate memory for the new entry
    struct ListNodeType* new_entry = (ListNodeType*)malloc(sizeof(struct ListNodeType));

    // Populate the entry
    new_entry->prev = NULL;
    new_entry->next = NULL;

    new_entry->child = sublist;

    if (display_name) strncpy(new_entry->display_name, display_name, LIST_NAME_MAX_STR);    // Cuts off long display names
    if (path) strncpy(new_entry->path, path, MENU_NAME_MAX_PATH);                           // Cuts off long file paths
    new_entry->selected = false;
    new_entry->updated = updated;

    // No list entries, insert as the head, set as active window head and select it
    if (list->head == NULL) {
        new_entry->prev = NULL;
        new_entry->selected = true;
        list->head = new_entry;
        list->active_head = new_entry;
        return;
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
}

// Deletes all nodes in the given list
void List::remove(ListType *list) {

    struct ListNodeType* entry = list->head;

    // Traverse the list until empty, clearing the memory for the nodes
    while(entry) {

        // Attach list head to next node
        list->head = entry->next;

        // Set new node to head unless it's empty
        if (list->head) {
            list->head->prev = NULL;
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
