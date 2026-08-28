#pragma once

#include "InventoryFields.h"

#include <optional>
#include <string>

#include <interop_contract/inventory/InventorySnapshot.hpp>

namespace RSCGroup::InventoryFieldUtil {

[[nodiscard]] std::optional<FieldValue> getFieldValue(const interop_contract::inventory::InventorySnapshot& snapshot, const std::string& fieldName);
[[nodiscard]] InventoryFields makeSingleFieldMap(const interop_contract::inventory::InventorySnapshot& snapshot, const std::string& fieldName);
[[nodiscard]] bool isMetadataField(const std::string& fieldName);

} // namespace RSCGroup::InventoryFieldUtil
