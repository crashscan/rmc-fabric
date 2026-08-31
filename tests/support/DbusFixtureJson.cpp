#include "DbusFixtureJson.h"

#include <dbus-cxx/variant.h>
#include <json/reader.h>

#include <fstream>
#include <stdexcept>
#include <vector>

namespace test_support {
namespace {

template <typename T>
T requireIntegral(const Json::Value& value, const char* typeName)
{
    if (!value.isIntegral()) {
        throw std::runtime_error(std::string("fixture value is not integral for type ") + typeName);
    }
    return static_cast<T>(value.asLargestInt());
}

template <>
std::uint64_t requireIntegral<std::uint64_t>(const Json::Value& value, const char* typeName)
{
    if (!value.isUInt64() && !value.isUInt() && !value.isIntegral()) {
        throw std::runtime_error(std::string("fixture value is not uint64 for type ") + typeName);
    }
    return value.asUInt64();
}

} // namespace

Json::Value loadJsonFile(const std::string& path)
{
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("failed to open fixture: " + path);
    }

    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    if (!parseFromStream(builder, in, &root, &errors)) {
        throw std::runtime_error("failed to parse fixture: " + path + ": " + errors);
    }
    return root;
}

DBus::Variant variantFromJson(const Json::Value& value)
{
    if (!value.isObject() || value.size() != 1U) {
        throw std::runtime_error("fixture variant must be a single-key object");
    }

    const auto members = value.getMemberNames();
    const std::string& type = members.front();
    const Json::Value& payload = value[type];

    if (type == "bool") {
        if (!payload.isBool()) {
            throw std::runtime_error("fixture bool payload must be bool");
        }
        return DBus::Variant(payload.asBool());
    }
    if (type == "i32") {
        return DBus::Variant(static_cast<std::int32_t>(requireIntegral<std::int32_t>(payload, "i32")));
    }
    if (type == "i64") {
        return DBus::Variant(static_cast<std::int64_t>(requireIntegral<std::int64_t>(payload, "i64")));
    }
    if (type == "u32") {
        return DBus::Variant(static_cast<std::uint32_t>(requireIntegral<std::uint32_t>(payload, "u32")));
    }
    if (type == "u64") {
        return DBus::Variant(requireIntegral<std::uint64_t>(payload, "u64"));
    }
    if (type == "string") {
        if (!payload.isString()) {
            throw std::runtime_error("fixture string payload must be string");
        }
        return DBus::Variant(payload.asString());
    }
    if (type == "list") {
        if (!payload.isArray()) {
            throw std::runtime_error("fixture list payload must be array");
        }
        std::vector<DBus::Variant> out;
        out.reserve(payload.size());
        for (const auto& item : payload) {
            out.push_back(variantFromJson(item));
        }
        return DBus::Variant(out);
    }
    if (type == "map") {
        return DBus::Variant(variantMapFromJsonObject(payload));
    }

    throw std::runtime_error("unsupported fixture variant type: " + type);
}

VariantMap variantMapFromJsonObject(const Json::Value& value)
{
    if (!value.isObject()) {
        throw std::runtime_error("fixture map payload must be object");
    }

    VariantMap out;
    for (const auto& name : value.getMemberNames()) {
        out.emplace(name, variantFromJson(value[name]));
    }
    return out;
}

NestedVariantMap nestedVariantMapFromJsonObject(const Json::Value& value)
{
    if (!value.isObject()) {
        throw std::runtime_error("fixture nested map payload must be object");
    }

    NestedVariantMap out;
    for (const auto& name : value.getMemberNames()) {
        out.emplace(name, variantMapFromJsonObject(value[name]));
    }
    return out;
}

Json::Value jsonFromVariant(const DBus::Variant& value)
{
    Json::Value out(Json::objectValue);
    switch (value.type()) {
        case DBus::DataType::BOOLEAN:
            out["bool"] = value.to_bool();
            return out;
        case DBus::DataType::INT32:
            out["i32"] = value.to_int32();
            return out;
        case DBus::DataType::INT64:
            out["i64"] = Json::Int64(value.to_int64());
            return out;
        case DBus::DataType::UINT32:
            out["u32"] = value.to_uint32();
            return out;
        case DBus::DataType::UINT64:
            out["u64"] = Json::UInt64(value.to_uint64());
            return out;
        case DBus::DataType::STRING:
            out["string"] = value.to_string();
            return out;
        case DBus::DataType::ARRAY: {
            Json::Value items(Json::arrayValue);
            for (const auto& item : value.to_vector<DBus::Variant>()) {
                items.append(jsonFromVariant(item));
            }
            out["list"] = std::move(items);
            return out;
        }
        case DBus::DataType::DICT_ENTRY:
        case DBus::DataType::VARIANT:
        case DBus::DataType::STRUCT:
        default: {
            Json::Value map(Json::objectValue);
            for (const auto& [key, nested] : value.to_map<std::string, DBus::Variant>()) {
                map[key] = jsonFromVariant(nested);
            }
            out["map"] = std::move(map);
            return out;
        }
    }
}

Json::Value jsonFromVariantMap(const VariantMap& value)
{
    Json::Value out(Json::objectValue);
    for (const auto& [key, item] : value) {
        out[key] = jsonFromVariant(item);
    }
    return out;
}

Json::Value jsonFromNestedVariantMap(const NestedVariantMap& value)
{
    Json::Value out(Json::objectValue);
    for (const auto& [key, item] : value) {
        out[key] = jsonFromVariantMap(item);
    }
    return out;
}

} // namespace test_support
