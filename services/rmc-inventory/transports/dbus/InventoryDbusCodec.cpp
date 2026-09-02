#include "InventoryDbusCodec.h"

#include <DecodeError.hpp>
#include <IngressLimits.hpp>

#include <dbus-cxx.h>

#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

using namespace interop_contract::inventory;

namespace RSCGroup::InventoryDbusCodec {
namespace {

using interop_contract::DecodeError;
using interop_contract::DecodeErrorCode;
using interop_contract::inventory::SourceStateMap;

void validateKey(const std::string& key)
{
    if (key.size() > interop_contract::ingress::kMaxKeyLength) {
        throw DecodeError(DecodeErrorCode::limit_exceeded, "oversized field key");
    }
}

void validateString(const std::string& value, const char* description)
{
    if (value.size() > interop_contract::ingress::kMaxStringLength) {
        throw DecodeError(DecodeErrorCode::limit_exceeded,
                          std::string(description) + " exceeds string ingress limit");
    }
}

void validateFieldMapSize(const VariantMap& map, std::size_t limit, const char* description)
{
    if (map.size() > limit) {
        throw DecodeError(DecodeErrorCode::limit_exceeded,
                          std::string(description) + " exceeds ingress limit");
    }
}

[[nodiscard]] const DBus::Variant& requireField(const VariantMap& map, std::string_view key)
{
    const auto it = map.find(std::string(key));
    if (it == map.end()) {
        throw DecodeError(DecodeErrorCode::missing_required_field,
                          "missing required field '" + std::string(key) + "'");
    }
    return it->second;
}

[[nodiscard]] int64_t varToInt64(const DBus::Variant& v)
{
    switch (v.type()) {
        case DBus::DataType::INT32:
            return v.to_int32();
        case DBus::DataType::INT64:
            return v.to_int64();
        case DBus::DataType::UINT32:
            return v.to_uint32();
        case DBus::DataType::UINT64: {
            const auto value = v.to_uint64();
            if (value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
                throw DecodeError(DecodeErrorCode::invalid_value,
                                  "unsigned integer does not fit into int64");
            }
            return static_cast<int64_t>(value);
        }
        default:
            throw DecodeError(DecodeErrorCode::invalid_type, "variant is not an integer");
    }
}

[[nodiscard]] uint64_t varToUint64(const DBus::Variant& v)
{
    switch (v.type()) {
        case DBus::DataType::UINT32:
            return v.to_uint32();
        case DBus::DataType::UINT64:
            return v.to_uint64();
        case DBus::DataType::INT32: {
            const auto value = v.to_int32();
            if (value < 0) {
                throw DecodeError(DecodeErrorCode::invalid_value,
                                  "negative value cannot decode as uint64");
            }
            return static_cast<uint64_t>(value);
        }
        case DBus::DataType::INT64: {
            const auto value = v.to_int64();
            if (value < 0) {
                throw DecodeError(DecodeErrorCode::invalid_value,
                                  "negative value cannot decode as uint64");
            }
            return static_cast<uint64_t>(value);
        }
        default:
            throw DecodeError(DecodeErrorCode::invalid_type,
                              "variant is not an unsigned integer");
    }
}

[[nodiscard]] bool requireBool(const VariantMap& map, std::string_view key)
{
    const auto& field = requireField(map, key);
    if (field.type() != DBus::DataType::BOOLEAN) {
        throw DecodeError(DecodeErrorCode::invalid_type,
                          "field '" + std::string(key) + "' is not a bool");
    }
    return field.to_bool();
}

[[nodiscard]] std::string requireString(const VariantMap& map, std::string_view key)
{
    const auto& field = requireField(map, key);
    if (field.type() != DBus::DataType::STRING) {
        throw DecodeError(DecodeErrorCode::invalid_type,
                          "field '" + std::string(key) + "' is not a string");
    }
    auto value = field.to_string();
    validateString(value, std::string(key).c_str());
    return value;
}

[[nodiscard]] std::string healthToString(SourceHealth health)
{
    switch (health) {
        case SourceHealth::OK:       return std::string(HEALTH_OK);
        case SourceHealth::DEGRADED: return std::string(HEALTH_DEGRADED);
        case SourceHealth::FAILED:   return std::string(HEALTH_FAILED);
    }
    return std::string(HEALTH_FAILED);
}

[[nodiscard]] SourceHealth healthFromString(const std::string& s)
{
    if (s == HEALTH_OK) {
        return SourceHealth::OK;
    }
    if (s == HEALTH_DEGRADED) {
        return SourceHealth::DEGRADED;
    }
    if (s == HEALTH_FAILED) {
        return SourceHealth::FAILED;
    }
    throw DecodeError(DecodeErrorCode::invalid_value,
                      "unknown source health '" + s + "'");
}

} // namespace

DBus::Variant encodeFieldValue(const FieldValue& value)
{
    return std::visit([](const auto& v) { return DBus::Variant(v); }, value);
}

VariantMap encodeFields(const InventoryFields& fields)
{
    VariantMap out;
    for (const auto& [key, value] : fields) {
        out[key] = encodeFieldValue(value);
    }
    return out;
}

VariantMap encodeSnapshot(const InventorySnapshot& snapshot)
{
    validateNoReservedKeysInFields(snapshot.fields);

    VariantMap out = encodeFields(snapshot.fields);
    out[std::string(FIELD_VERSION)]   = DBus::Variant(snapshot.version);
    out[std::string(FIELD_TIMESTAMP)] = DBus::Variant(snapshot.timestamp);
    out[std::string(FIELD_READY)]     = DBus::Variant(snapshot.ready);
    out[std::string(FIELD_PHASE)]     = DBus::Variant(snapshot.phase);
    return out;
}

VariantMap encodeSourceState(const SourceState& state)
{
    VariantMap m;
    m[std::string(SOURCE_STATE_HEALTH)]          = DBus::Variant(healthToString(state.health));
    m[std::string(SOURCE_STATE_REQUIRED)]        = DBus::Variant(state.required);
    m[std::string(SOURCE_STATE_STALE)]           = DBus::Variant(state.stale);
    m[std::string(SOURCE_STATE_LAST_ATTEMPT_TS)] = DBus::Variant(state.lastAttemptTs);
    m[std::string(SOURCE_STATE_LAST_SUCCESS_TS)] = DBus::Variant(state.lastSuccessTs);
    if (state.lastError) {
        m[std::string(SOURCE_STATE_LAST_ERROR)] = DBus::Variant(*state.lastError);
    }
    if (state.origin) {
        m[std::string(SOURCE_STATE_ORIGIN)] = DBus::Variant(*state.origin);
    }
    return m;
}

NestedVariantMap encodeSourceStates(const SourceStateMap& states)
{
    NestedVariantMap out;
    for (const auto& [name, state] : states) {
        out[name] = encodeSourceState(state);
    }
    return out;
}

FieldValue decodeFieldValue(const DBus::Variant& value)
{
    switch (value.type()) {
        case DBus::DataType::BOOLEAN:
            return value.to_bool();
        case DBus::DataType::INT32:
            return static_cast<int64_t>(value.to_int32());
        case DBus::DataType::INT64:
            return value.to_int64();
        case DBus::DataType::UINT32:
            return static_cast<uint64_t>(value.to_uint32());
        case DBus::DataType::UINT64:
            return value.to_uint64();
        case DBus::DataType::STRING: {
            auto text = value.to_string();
            validateString(text, "field value");
            return text;
        }
        default:
            throw DecodeError(DecodeErrorCode::invalid_type,
                              "decodeFieldValue: unsupported variant type");
    }
}

InventoryFields decodeFields(const VariantMap& map)
{
    validateFieldMapSize(map, interop_contract::ingress::inventory::kMaxFields, "inventory field map");

    InventoryFields out;
    for (const auto& [key, var] : map) {
        validateKey(key);
        out[key] = decodeFieldValue(var);
    }
    return out;
}

InventorySnapshot decodeSnapshot(const VariantMap& map)
{
    validateFieldMapSize(map, interop_contract::ingress::inventory::kMaxFields + 4,
                         "inventory snapshot");

    InventorySnapshot snap;
    snap.version = varToUint64(requireField(map, FIELD_VERSION));
    snap.timestamp = varToInt64(requireField(map, FIELD_TIMESTAMP));
    snap.ready = requireBool(map, FIELD_READY);
    snap.phase = requireString(map, FIELD_PHASE);

    for (const auto& [key, var] : map) {
        validateKey(key);
        if (key == FIELD_VERSION || key == FIELD_TIMESTAMP ||
            key == FIELD_READY || key == FIELD_PHASE) {
            continue;
        }
        snap.fields[key] = decodeFieldValue(var);
    }
    return snap;
}

SourceState decodeSourceState(const VariantMap& map, const std::string& sourceName)
{
    validateFieldMapSize(map, 7, "inventory source state");
    validateString(sourceName, "source name");

    SourceState s;
    s.name = sourceName;
    s.health = healthFromString(requireString(map, SOURCE_STATE_HEALTH));
    s.required = requireBool(map, SOURCE_STATE_REQUIRED);
    s.stale = requireBool(map, SOURCE_STATE_STALE);
    s.lastAttemptTs = varToInt64(requireField(map, SOURCE_STATE_LAST_ATTEMPT_TS));
    s.lastSuccessTs = varToInt64(requireField(map, SOURCE_STATE_LAST_SUCCESS_TS));

    if (const auto it = map.find(std::string(SOURCE_STATE_LAST_ERROR)); it != map.end()) {
        if (it->second.type() != DBus::DataType::STRING) {
            throw DecodeError(DecodeErrorCode::invalid_type,
                              "field '" + std::string(SOURCE_STATE_LAST_ERROR) + "' is not a string");
        }
        auto value = it->second.to_string();
        validateString(value, std::string(SOURCE_STATE_LAST_ERROR).c_str());
        s.lastError = std::move(value);
    }
    if (const auto it = map.find(std::string(SOURCE_STATE_ORIGIN)); it != map.end()) {
        if (it->second.type() != DBus::DataType::STRING) {
            throw DecodeError(DecodeErrorCode::invalid_type,
                              "field '" + std::string(SOURCE_STATE_ORIGIN) + "' is not a string");
        }
        auto value = it->second.to_string();
        validateString(value, std::string(SOURCE_STATE_ORIGIN).c_str());
        s.origin = std::move(value);
    }
    return s;
}

SourceStateMap decodeSourceStates(const NestedVariantMap& states)
{
    if (states.size() > interop_contract::ingress::inventory::kMaxSources) {
        throw DecodeError(DecodeErrorCode::limit_exceeded,
                          "inventory source states exceed ingress limit");
    }

    SourceStateMap out;
    for (const auto& [name, inner] : states) {
        validateKey(name);
        out[name] = decodeSourceState(inner, name);
    }
    return out;
}

bool isReservedMetadataKey(const std::string& key)
{
    return interop_contract::inventory::is_metadata_field(key);
}

void validateNoReservedKeysInFields(const InventoryFields& fields)
{
    if (const auto key = interop_contract::inventory::first_reserved_metadata_field(fields); !key.empty()) {
        throw std::runtime_error(
            "InventoryDbusCodec: reserved metadata key '" + key +
            "' present in source-owned fields");
    }
}

NestedVariantMap encodeIssues(const interop_contract::inventory::InventoryIssues& issues)
{
    NestedVariantMap out;
    for (const auto& [name, issue] : issues) {
        out[name] = encodeFields(issue);
    }
    return out;
}

interop_contract::inventory::InventoryIssues decodeIssues(const NestedVariantMap& map)
{
    if (map.size() > interop_contract::ingress::inventory::kMaxIssues) {
        throw DecodeError(DecodeErrorCode::limit_exceeded,
                          "inventory issues exceed ingress limit");
    }

    interop_contract::inventory::InventoryIssues out;
    for (const auto& [name, inner] : map) {
        validateKey(name);
        validateFieldMapSize(inner, interop_contract::ingress::inventory::kMaxIssueFields,
                             "inventory issue");
        out[name] = decodeFields(inner);
    }
    return out;
}

} // namespace RSCGroup::InventoryDbusCodec
