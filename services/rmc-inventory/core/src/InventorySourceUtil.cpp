#include "core/InventorySourceUtil.h"

#include <algorithm>
#include <ranges>

namespace RSCGroup::InventorySourceUtil {

bool isSubsetOfOwnedFields(const InventoryFields& fields, const FieldNameList& ownedFields)
{
    for (const auto& key: fields | std::views::keys) {
        if (std::ranges::find(ownedFields, key) == ownedFields.end()) {
            return false;
        }
    }
    return true;
}

FieldNameList getUndeclaredFields(const InventoryFields& fields, const FieldNameList& ownedFields)
{
    FieldNameList undeclared;
    for (const auto& [key, _] : fields) {
        if (std::ranges::find(ownedFields, key) == ownedFields.end()) {
            undeclared.push_back(key);
        }
    }
    return undeclared;
}

} // namespace RSCGroup::InventorySourceUtil