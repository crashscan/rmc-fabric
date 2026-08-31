#pragma once

#include <json/value.h>

#include <map>
#include <string>

namespace DBus {
class Variant;
}

namespace test_support {

using VariantMap = std::map<std::string, DBus::Variant>;
using NestedVariantMap = std::map<std::string, VariantMap>;

Json::Value loadJsonFile(const std::string& path);
DBus::Variant variantFromJson(const Json::Value& value);
VariantMap variantMapFromJsonObject(const Json::Value& value);
NestedVariantMap nestedVariantMapFromJsonObject(const Json::Value& value);

Json::Value jsonFromVariant(const DBus::Variant& value);
Json::Value jsonFromVariantMap(const VariantMap& value);
Json::Value jsonFromNestedVariantMap(const NestedVariantMap& value);

} // namespace test_support
