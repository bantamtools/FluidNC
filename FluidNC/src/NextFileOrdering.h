// NextFileOrdering.h
//
// User preference for which file the future "Plot Next" feature
// should pick when starting an unattended run. Persisted in NVS as
// an EnumSetting (key: Bantam/NextFileOrdering). The actual sort /
// selection logic is the consumer's responsibility — this module
// just stores the preference and exposes a typed accessor.
//
// Design spec: docs/plans/2026-04-29-next-file-ordering-design.md
// Issue: internal tracker

#pragma once

#include <cstdint>

namespace NextFileOrdering {

// Stable numeric mapping: saved NVS values survive firmware upgrades.
enum class Order : int8_t {
    Oldest = 0,
    Newest = 1,
    A_to_Z = 2,
    Z_to_A = 3,
};

// Reads the NVS-backed setting. Null-safe in early boot (returns
// Order::Oldest before make_settings() has run).
//
// Live values written through the EnumSetting interface are already
// validated by EnumSetting against the options map, so an in-range
// program-set value is guaranteed at runtime. The clamp below exists
// for the firmware-downgrade case: a future firmware version may add
// a 5th option (numeric value 4) and persist it; if a user then
// flashes back to this firmware, get() must produce a defined Order
// rather than an undefined cast.
Order get();

// Pure helper exposed for testing.
//
// Threat model: defends against an out-of-range int read from NVS,
// not against in-program corruption. Older firmware reads of values
// written by newer firmware are the case this matters for.
constexpr Order clamp_to_order(int raw) {
    return (raw < 0 || raw > 3) ? Order::Oldest : static_cast<Order>(raw);
}

}  // namespace NextFileOrdering
