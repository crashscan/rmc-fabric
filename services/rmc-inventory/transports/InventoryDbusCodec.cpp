#include "InventoryDbusCodec.h"

#include <dbus-cxx.h>
#include <stdexcept>
#include <utility>

using namespace interop_contract::inventory;

namespace RSCGroup::InventoryDbusCodec {
namespace {

using interop_contract::inventory::SourceStateMap;

[[nodiscard]] int64_t varToInt64(const DBus::Variant& v)
{
    switch (v.type()) {
        case DBus::DataType::INT32:  return v.to_int32();
        case DBus::DataType::INT64:  return v.to_int64();
        case DBus::DataType::UINT32: return v.to_uint32();
        case DBus::DataType::UINT64: return static_cast<int64_t>(v.to_uint64());
        default: throw std::runtime_error("variant is not an integer");
    }
}

[[nodiscard]] uint64_t varToUint64(const DBus::Variant& v)
{
    switch (v.type()) {
        case DBus::DataType::UINT32: return v.to_uint32();
        case DBus::DataType::UINT64: return v.to_uint64();
        case DBus::DataType::INT32: {
            const auto x = v.to_int32();
            if (x < 0) throw std::runtime_error("negative value cannot decode as uint64");
            return static_cast<uint64_t>(x);
        }
        case DBus::DataType::INT64: {
            const auto x = v.to_int64();
            if (x < 0) throw std::runtime_error("negative value cannot decode as uint64");
            return static_cast<uint64_t>(x);
        }
        default: throw std::runtime_error("variant is not an unsigned integer");
    }
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
    if (s == HEALTH_OK)       return SourceHealth::OK;
    if (s == HEALTH_DEGRADED) return SourceHealth::DEGRADED;
    return SourceHealth::FAILED;
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
        case DBus::DataType::BOOLEAN: return value.to_bool();
        case DBus::DataType::INT32:   return static_cast<int64_t>(value.to_int32());
        case DBus::DataType::INT64:   return value.to_int64();
        case DBus::DataType::UINT32:  return static_cast<uint64_t>(value.to_uint32());
        case DBus::DataType::UINT64:  return value.to_uint64();
        case DBus::DataType::STRING:  return value.to_string();
        default:
            throw std::runtime_error("decodeFieldValue: unsupported variant type");
    }
}

InventoryFields decodeFields(const VariantMap& map)
{
    InventoryFields out;
    for (const auto& [key, var] : map) {
        out[key] = decodeFieldValue(var);
    }
    return out;
}

InventorySnapshot decodeSnapshot(const VariantMap& map)
{
    InventorySnapshot snap;
    for (const auto& [key, var] : map) {
        if (key == FIELD_VERSION)        snap.version   = varToUint64(var);
        else if (key == FIELD_TIMESTAMP) snap.timestamp = varToInt64(var);
        else if (key == FIELD_READY)     snap.ready     = var.to_bool();
        else if (key == FIELD_PHASE)     snap.phase     = var.to_string();
        else snap.fields[key] = decodeFieldValue(var);
    }
    return snap;
}

SourceState decodeSourceState(const VariantMap& map, const std::string& sourceName)
{
    SourceState s;
    s.name = sourceName;

    auto str = [&](const char* k) -> std::string {
        return map.contains(k) ? map.at(k).to_string() : std::string{};
    };
    auto boolean = [&](const char* k) -> bool {
        return map.contains(k) && map.at(k).to_bool();
    };
    auto num = [&](const char* k) -> int64_t {
        return map.contains(k) ? varToInt64(map.at(k)) : 0;
    };

    const std::string healthKey    = std::string(SOURCE_STATE_HEALTH);
    const std::string requiredKey  = std::string(SOURCE_STATE_REQUIRED);
    const std::string staleKey     = std::string(SOURCE_STATE_STALE);
    const std::string attemptKey   = std::string(SOURCE_STATE_LAST_ATTEMPT_TS);
    const std::string successKey   = std::string(SOURCE_STATE_LAST_SUCCESS_TS);
    const std::string lastErrorKey = std::string(SOURCE_STATE_LAST_ERROR);

    s.health        = healthFromString(str(healthKey.c_str()));
    s.required      = boolean(requiredKey.c_str());
    s.stale         = boolean(staleKey.c_str());
    s.lastAttemptTs = num(attemptKey.c_str());
    s.lastSuccessTs = num(successKey.c_str());

    if (map.contains(lastErrorKey)) {
        s.lastError = map.at(lastErrorKey).to_string();
    }
    if (const auto originKey = std::string(SOURCE_STATE_ORIGIN); map.contains(originKey)) {
        s.origin = map.at(originKey).to_string();
    }
    return s;
}

SourceStateMap decodeSourceStates(const NestedVariantMap& states)
{
    SourceStateMap out;
    for (const auto& [name, inner] : states) {
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
    interop_contract::inventory::InventoryIssues out;
    for (const auto& [name, inner] : map) {
        out[name] = decodeFields(inner);
    }
    return out;
}

} // namespace RSCGroup::InventoryDbusCodec
