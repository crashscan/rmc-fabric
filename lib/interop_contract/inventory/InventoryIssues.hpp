#pragma once

#include "InventoryTypes.hpp"

#include <map>
#include <string>

namespace interop_contract::inventory {

using InventoryIssueFields = InventoryFields;
using InventoryIssues = std::map<std::string, InventoryIssueFields>;

} // namespace interop_contract::inventory