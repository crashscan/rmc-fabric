#pragma once

#include <interop_contract/inventory.hpp>

#include <map>
#include <string>

namespace DBus {
class Variant;
}

namespace RSCGroup::InventoryDbusCodec {

using VariantMap = std::map<std::string, DBus::Variant>;
using NestedVariantMap = std::map<std::string, VariantMap>;

[[nodiscard]] DBus::Variant encodeFieldValue(const interop_contract::inventory::FieldValue& value);
[[nodiscard]] VariantMap encodeFields(const interop_contract::inventory::InventoryFields& fields);
[[nodiscard]] VariantMap encodeSnapshot(const interop_contract::inventory::InventorySnapshot& snapshot);

[[nodiscard]] VariantMap encodeSourceState(const interop_contract::inventory::SourceState& state);
[[nodiscard]] NestedVariantMap encodeSourceStates(const interop_contract::inventory::SourceStateMap& states);

[[nodiscard]] interop_contract::inventory::FieldValue decodeFieldValue(const DBus::Variant& value);
[[nodiscard]] interop_contract::inventory::InventoryFields decodeFields(const VariantMap& map);
[[nodiscard]] interop_contract::inventory::InventorySnapshot decodeSnapshot(const VariantMap& map);

[[nodiscard]] interop_contract::inventory::SourceState decodeSourceState(const VariantMap& map, const std::string& sourceName);
[[nodiscard]] interop_contract::inventory::SourceStateMap decodeSourceStates(const NestedVariantMap& states);

[[nodiscard]] NestedVariantMap encodeIssues(const interop_contract::inventory::InventoryIssues& issues);
[[nodiscard]] interop_contract::inventory::InventoryIssues decodeIssues(const NestedVariantMap& map);

[[nodiscard]] bool isReservedMetadataKey(const std::string& key);
void validateNoReservedKeysInFields(const interop_contract::inventory::InventoryFields& fields);

} // namespace RSCGroup::InventoryDbusCodec
