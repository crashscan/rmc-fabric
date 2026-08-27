#pragma once

#include <interop_contract/inventory/InventoryIssues.hpp>
#include <interop_contract/inventory/InventorySourceState.hpp>
#include <interop_contract/inventory/InventoryTypes.hpp>

namespace RSCGroup {

using FieldValue = interop_contract::inventory::FieldValue;
using InventoryFields = interop_contract::inventory::InventoryFields;
using FieldNameList = interop_contract::inventory::FieldNameList;
using InventoryIssues = interop_contract::inventory::InventoryIssues;
using SourceHealth = interop_contract::inventory::SourceHealth;
using SourceState = interop_contract::inventory::SourceState;

struct InventoryDiff {
    FieldNameList changedFields;
    FieldNameList removedFields;

    [[nodiscard]] bool empty() const {
        return changedFields.empty() && removedFields.empty();
    }
};

} // namespace RSCGroup