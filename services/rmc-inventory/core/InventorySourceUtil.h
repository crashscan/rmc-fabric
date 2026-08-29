#pragma once

#include "InventoryTypes.h"

namespace RSCGroup::InventorySourceUtil {

[[nodiscard]] bool isSubsetOfOwnedFields(const InventoryFields& fields, const FieldNameList& ownedFields);

[[nodiscard]] FieldNameList getUndeclaredFields(const InventoryFields& fields, const FieldNameList& ownedFields);

} // namespace RSCGroup::InventorySourceUtil