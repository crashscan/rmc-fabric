#pragma once

#include "InventoryContracts.hpp"
#include "InventorySnapshot.hpp"

#include <optional>
#include <string>

namespace interop_contract::inventory {

[[nodiscard]] inline std::optional<FieldValue> get_field_value(const InventorySnapshot& snapshot,
                                                               const std::string& field_name)
{
    if (const auto it = snapshot.fields.find(field_name); it != snapshot.fields.end()) {
        return it->second;
    }

    if (field_name == FIELD_VERSION) {
        return FieldValue{snapshot.version};
    }
    if (field_name == FIELD_TIMESTAMP) {
        return FieldValue{snapshot.timestamp};
    }
    if (field_name == FIELD_READY) {
        return FieldValue{snapshot.ready};
    }
    if (field_name == FIELD_PHASE) {
        return FieldValue{snapshot.phase};
    }

    return std::nullopt;
}

[[nodiscard]] inline InventoryFields make_single_field_map(const InventorySnapshot& snapshot,
                                                           const std::string& field_name)
{
    if (const auto value = get_field_value(snapshot, field_name); value) {
        return {{field_name, value.value()}};
    }
    return {};
}

} // namespace interop_contract::inventory