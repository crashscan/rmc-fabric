#pragma once

#include "interop_contract/inventory/InventoryContracts.hpp"

#include <string>
#include <string_view>

namespace interop_contract::inventory {

[[nodiscard]] inline bool is_metadata_field(std::string_view field_name)
{
    return field_name == FIELD_VERSION ||
           field_name == FIELD_TIMESTAMP ||
           field_name == FIELD_READY ||
           field_name == FIELD_PHASE;
}

[[nodiscard]] inline bool is_metadata_field(const std::string& field_name)
{
    return is_metadata_field(std::string_view(field_name));
}

} // namespace interop_contract::inventory