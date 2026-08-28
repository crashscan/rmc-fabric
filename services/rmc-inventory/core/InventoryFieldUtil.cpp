#include "InventoryFieldUtil.h"

#include <interop_contract/inventory.hpp>

namespace RSCGroup::InventoryFieldUtil {

std::optional<FieldValue> getFieldValue(const interop_contract::inventory::InventorySnapshot& snapshot, const std::string& fieldName)
{
    return interop_contract::inventory::get_field_value(snapshot, fieldName);
}

InventoryFields makeSingleFieldMap(const interop_contract::inventory::InventorySnapshot& snapshot, const std::string& fieldName)
{
    return interop_contract::inventory::make_single_field_map(snapshot, fieldName);
}

bool isMetadataField(const std::string& fieldName)
{
    return interop_contract::inventory::is_metadata_field(fieldName);
}

} // namespace RSCGroup::InventoryFieldUtil
