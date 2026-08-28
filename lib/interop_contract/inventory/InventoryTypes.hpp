#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace interop_contract::inventory {

using FieldValue = std::variant<bool, int64_t, uint64_t, std::string>;
using InventoryFields = std::map<std::string, FieldValue>;
using FieldNameList = std::vector<std::string>;

} // namespace interop_contract::inventory