// NextFileOrdering.cpp — see NextFileOrdering.h for the design.

#include "NextFileOrdering.h"

#include "SettingsDefinitions.h"

namespace NextFileOrdering {

Order get() {
    // Null guard: handles the early-boot window before
    // make_settings() has run. Treat as Oldest (the registered
    // default) rather than crash.
    if (next_file_ordering == nullptr) {
        return Order::Oldest;
    }
    return clamp_to_order(next_file_ordering->get());
}

}  // namespace NextFileOrdering
