#pragma once

#include "InventoryMetadata.hpp"
#include "InventoryTypes.hpp"

#include <algorithm>
#include <ranges>
#include <string>

namespace interop_contract::inventory {

[[nodiscard]] inline bool contains_reserved_metadata_fields(const InventoryFields& fields)
{
    return std::ranges::any_of(fields, [](const auto& entry) {
        return is_metadata_field(entry.first);
    });
}

[[nodiscard]] inline std::string first_reserved_metadata_field(const InventoryFields& fields)
{
    for (const auto& key : fields | std::views::keys) {
        if (is_metadata_field(key)) {
            return key;
        }
    }
    return {};
}

} // namespace interop_contract::inventory