#pragma once

#include "InventoryTypes.hpp"

#include <cstdint>
#include <string>

namespace interop_contract::inventory {

struct InventorySnapshot {
    uint64_t version{0};
    int64_t timestamp{0};
    bool ready{false};
    std::string phase;
    InventoryFields fields;
};

} // namespace interop_contract::inventory