#pragma once

#include "core/InventoryFields.h"

#include <interop_contract/inventory/InventorySourceState.hpp>

namespace RSCGroup::InventoryIssueUtil {

[[nodiscard]] interop_contract::inventory::InventoryIssues deriveIssues(
    const interop_contract::inventory::SourceStateMap& states);

} // namespace RSCGroup::InventoryIssueUtil