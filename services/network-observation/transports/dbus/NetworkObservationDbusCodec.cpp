#include "NetworkObservationDbusCodec.h"

#include <DecodeError.hpp>
#include <IngressLimits.hpp>
#include <network_observation/NetworkObservationEnumStrings.hpp>

#include <dbus-cxx.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace RSCGroup::NetworkObservationDbusCodec {

namespace contract = interop_contract::network_observation;

namespace {

using interop_contract::DecodeError;
using interop_contract::DecodeErrorCode;

[[nodiscard]] const DBus::Variant& requireField(const std::map<std::string, DBus::Variant>& m,
                                                std::string_view key)
{
    const auto it = m.find(std::string(key));
    if (it == m.end()) {
        throw DecodeError(DecodeErrorCode::missing_required_field,
                          "missing required field '" + std::string(key) + "'");
    }
    return it->second;
}

void validateKey(const std::string& key)
{
    if (key.size() > interop_contract::ingress::kMaxKeyLength) {
        throw DecodeError(DecodeErrorCode::limit_exceeded, "oversized field key");
    }
}

void validateString(const std::string& value, const char* fieldName)
{
    if (value.size() > interop_contract::ingress::kMaxStringLength) {
        throw DecodeError(DecodeErrorCode::limit_exceeded,
                          std::string(fieldName) + " exceeds string ingress limit");
    }
}

void validateStringSet(const std::set<std::string>& values, const char* fieldName)
{
    if (values.size() > interop_contract::ingress::network_observation::kMaxStringSetEntries) {
        throw DecodeError(DecodeErrorCode::limit_exceeded, std::string(fieldName) + " exceeds collection limit");
    }
    for (const auto& value : values) validateString(value, fieldName);
}

[[nodiscard]] std::string requireString(const std::map<std::string, DBus::Variant>& m, std::string_view key)
{
    validateKey(std::string(key));
    const auto& field = requireField(m, key);
    if (field.type() != DBus::DataType::STRING) {
        throw DecodeError(DecodeErrorCode::invalid_type,
                          "field '" + std::string(key) + "' is not a string");
    }
    auto value = field.to_string();
    validateString(value, std::string(key).c_str());
    return value;
}

[[nodiscard]] std::optional<std::string> optionalString(const std::map<std::string, DBus::Variant>& m,
                                                        std::string_view key)
{
    const auto it = m.find(std::string(key));
    if (it == m.end()) {
        return std::nullopt;
    }
    if (it->second.type() != DBus::DataType::STRING) {
        throw DecodeError(DecodeErrorCode::invalid_type,
                          "field '" + std::string(key) + "' is not a string");
    }
    auto value = it->second.to_string();
    validateString(value, std::string(key).c_str());
    return value;
}

[[nodiscard]] bool requireBool(const std::map<std::string, DBus::Variant>& m, std::string_view key)
{
    validateKey(std::string(key));
    const auto& field = requireField(m, key);
    if (field.type() != DBus::DataType::BOOLEAN) {
        throw DecodeError(DecodeErrorCode::invalid_type,
                          "field '" + std::string(key) + "' is not a bool");
    }
    return field.to_bool();
}

[[nodiscard]] int requireInt32(const std::map<std::string, DBus::Variant>& m, std::string_view key)
{
    validateKey(std::string(key));
    const auto& field = requireField(m, key);
    if (field.type() != DBus::DataType::INT32) {
        throw DecodeError(DecodeErrorCode::invalid_type,
                          "field '" + std::string(key) + "' is not an int32");
    }
    return static_cast<int>(field.to_int32());
}

[[nodiscard]] std::set<std::string> requireStringSet(const std::map<std::string, DBus::Variant>& m,
                                                     std::string_view key)
{
    validateKey(std::string(key));
    auto varCopy = requireField(m, key);
    std::set<std::string> out;
    std::vector<DBus::Variant> values;
    try {
        values = varCopy.to_vector<DBus::Variant>();
    } catch (const std::exception& e) {
        throw DecodeError(DecodeErrorCode::invalid_type,
                          "field '" + std::string(key) + "' is not a string collection: " + e.what());
    }
    if (values.size() > interop_contract::ingress::network_observation::kMaxStringSetEntries) {
        throw DecodeError(DecodeErrorCode::limit_exceeded,
                          "field '" + std::string(key) + "' exceeds collection ingress limit");
    }
    for (const auto& value : values) {
        if (value.type() != DBus::DataType::STRING) {
            throw DecodeError(DecodeErrorCode::invalid_type,
                              "field '" + std::string(key) + "' contains a non-string element");
        }
        auto item = value.to_string();
        validateString(item, std::string(key).c_str());
        out.insert(std::move(item));
    }
    return out;
}

[[nodiscard]] contract::CandidateClassification parseClassification(const std::string& value)
{
    const auto parsed = contract::classificationFromString(value);
    if (parsed == contract::CandidateClassification::Unknown && value != "Unknown") {
        throw DecodeError(DecodeErrorCode::invalid_value,
                          "unknown candidate classification '" + value + "'");
    }
    return parsed;
}

[[nodiscard]] contract::CandidateStatus parseStatus(const std::string& value)
{
    if (value == "Provisional") return contract::CandidateStatus::Provisional;
    if (value == "Confirmed") return contract::CandidateStatus::Confirmed;
    if (value == "Aged") return contract::CandidateStatus::Aged;
    if (value == "Expired") return contract::CandidateStatus::Expired;
    if (value == "Removed") return contract::CandidateStatus::Removed;
    throw DecodeError(DecodeErrorCode::invalid_value,
                      "unknown candidate status '" + value + "'");
}

void validateMapLimit(const std::map<std::string, DBus::Variant>& m,
                      std::size_t limit,
                      const char* description)
{
    if (m.size() > limit) {
        throw DecodeError(DecodeErrorCode::limit_exceeded,
                          std::string(description) + " exceeds ingress limit");
    }
}

} // namespace

std::map<std::string, DBus::Variant> toVariantMap(const contract::LocalInterfaceState& iface)
{
    validateString(iface.ifname, "ifname");
    validateString(iface.mac, "mac");
    validateString(iface.operstate, "operstate");
    if (iface.masterIfname) validateString(*iface.masterIfname, "master");
    validateStringSet(iface.ipv4, "ipv4");
    validateStringSet(iface.ipv6, "ipv6");

    std::map<std::string, DBus::Variant> d;
    d[std::string(contract::K_IFINDEX)]   = DBus::Variant(static_cast<int32_t>(iface.ifindex));
    d[std::string(contract::K_IFNAME)]    = DBus::Variant(iface.ifname);
    d[std::string(contract::K_MAC)]       = DBus::Variant(iface.mac);
    d[std::string(contract::K_ADMINUP)]   = DBus::Variant(iface.adminUp);
    d[std::string(contract::K_RUNNING)]   = DBus::Variant(iface.running);
    d[std::string(contract::K_OPERSTATE)] = DBus::Variant(iface.operstate);
    if (iface.masterIfname)
        d[std::string(contract::K_MASTER)] = DBus::Variant(*iface.masterIfname);

    std::vector<DBus::Variant> ip4;
    for (const auto& a : iface.ipv4) ip4.emplace_back(a);
    d[std::string(contract::K_IPV4)] = DBus::Variant(ip4);

    std::vector<DBus::Variant> ip6;
    for (const auto& a : iface.ipv6) ip6.emplace_back(a);
    d[std::string(contract::K_IPV6)] = DBus::Variant(ip6);

    return d;
}

std::map<std::string, DBus::Variant> toVariantMap(const contract::RemoteCandidate& c)
{
    validateString(c.mac, "mac");
    if (c.bridgePort) validateString(*c.bridgePort, "bridgePort");
    if (c.remoteChassisId) validateString(*c.remoteChassisId, "remoteChassisId");
    if (c.remotePortId) validateString(*c.remotePortId, "remotePortId");
    if (c.remoteSystemName) validateString(*c.remoteSystemName, "remoteSystemName");
    validateStringSet(c.neighborIfaces, "neighborIfaces");
    validateStringSet(c.ipv4, "ipv4");
    validateStringSet(c.ipv6, "ipv6");

    std::map<std::string, DBus::Variant> cand;
    cand[std::string(contract::K_MAC)]            = DBus::Variant(c.mac);
    cand[std::string(contract::K_CLASSIFICATION)] = DBus::Variant(contract::classificationToString(c.classification));
    cand[std::string(contract::K_STATUS)]         = DBus::Variant(contract::statusToString(c.status));
    cand[std::string(contract::K_SEEN_IN_FDB)]    = DBus::Variant(c.seenInFdb);
    cand[std::string(contract::K_SEEN_IN_NEIGH)]  = DBus::Variant(c.seenInNeigh);
    cand[std::string(contract::K_SEEN_IN_LLDP)]   = DBus::Variant(c.seenInLldp);
    if (c.bridgePort)
        cand[std::string(contract::K_BRIDGE_PORT)]        = DBus::Variant(*c.bridgePort);
    if (c.remoteChassisId)
        cand[std::string(contract::K_REMOTE_CHASSIS_ID)]  = DBus::Variant(*c.remoteChassisId);
    if (c.remotePortId)
        cand[std::string(contract::K_REMOTE_PORT_ID)]     = DBus::Variant(*c.remotePortId);
    if (c.remoteSystemName)
        cand[std::string(contract::K_REMOTE_SYSTEM_NAME)] = DBus::Variant(*c.remoteSystemName);

    std::vector<DBus::Variant> neighIfaces;
    for (const auto& nif : c.neighborIfaces) neighIfaces.emplace_back(nif);
    cand[std::string(contract::K_NEIGHBOR_IFACES)] = DBus::Variant(neighIfaces);

    std::vector<DBus::Variant> ip4;
    for (const auto& ip : c.ipv4) ip4.emplace_back(ip);
    cand[std::string(contract::K_IPV4)] = DBus::Variant(ip4);

    std::vector<DBus::Variant> ip6;
    for (const auto& ip : c.ipv6) ip6.emplace_back(ip);
    cand[std::string(contract::K_IPV6)] = DBus::Variant(ip6);

    return cand;
}

std::map<std::string, std::map<std::string, DBus::Variant>> encodeIssues(const contract::ObservationIssues& issues)
{
    if (issues.size() > interop_contract::ingress::network_observation::kMaxIssues) {
        throw DecodeError(DecodeErrorCode::limit_exceeded, "issues map exceeds contract limit");
    }

    std::map<std::string, std::map<std::string, DBus::Variant>> encoded;
    for (const auto& [issueCode, fields] : issues) {
        validateKey(issueCode);
        if (fields.size() > interop_contract::ingress::network_observation::kMaxIssueFields) {
            throw DecodeError(DecodeErrorCode::limit_exceeded, "issue fields map exceeds contract limit");
        }

        std::map<std::string, DBus::Variant> encodedFields;
        for (const auto& [name, value] : fields) {
            validateKey(name);
            validateString(value, name.c_str());
            encodedFields.emplace(name, DBus::Variant(value));
        }
        encoded.emplace(issueCode, std::move(encodedFields));
    }
    return encoded;
}

contract::LocalNetworkSnapshot
fromVariantMapLocalSnapshot(const std::map<std::string, DBus::Variant>& m)
{
    if (m.size() > interop_contract::ingress::network_observation::kMaxInterfaces) {
        throw DecodeError(DecodeErrorCode::limit_exceeded,
                          "local snapshot exceeds interface ingress limit");
    }

    contract::LocalNetworkSnapshot snapshot;
    for (const auto& [name, value] : m) {
        validateString(name, "interface name");
        auto ifaceMap = value;
        try {
            snapshot.interfaces[name] = fromVariantMapIface(
                ifaceMap.to_map<std::string, DBus::Variant>());
        } catch (const DecodeError&) {
            throw;
        } catch (const std::exception& e) {
            throw DecodeError(DecodeErrorCode::invalid_type,
                              "local snapshot interface entry '" + name +
                                  "' is not a variant map: " + e.what());
        }
    }
    return snapshot;
}

contract::LocalInterfaceState
fromVariantMapIface(const std::map<std::string, DBus::Variant>& m)
{
    validateMapLimit(m, 9, "local interface payload");

    contract::LocalInterfaceState s;
    s.ifindex   = requireInt32(m, contract::K_IFINDEX);
    s.ifname    = requireString(m, contract::K_IFNAME);
    s.mac       = requireString(m, contract::K_MAC);
    s.adminUp   = requireBool(m, contract::K_ADMINUP);
    s.running   = requireBool(m, contract::K_RUNNING);
    s.operstate = requireString(m, contract::K_OPERSTATE);
    s.masterIfname = optionalString(m, contract::K_MASTER);
    s.ipv4 = requireStringSet(m, contract::K_IPV4);
    s.ipv6 = requireStringSet(m, contract::K_IPV6);
    return s;
}

contract::RemoteCandidate
fromVariantMapCandidate(const std::map<std::string, DBus::Variant>& m)
{
    validateMapLimit(m, 13, "remote candidate payload");

    contract::RemoteCandidate c;
    c.mac            = requireString(m, contract::K_MAC);
    c.classification = parseClassification(requireString(m, contract::K_CLASSIFICATION));
    c.status         = parseStatus(requireString(m, contract::K_STATUS));
    c.seenInFdb      = requireBool(m, contract::K_SEEN_IN_FDB);
    c.seenInNeigh    = requireBool(m, contract::K_SEEN_IN_NEIGH);
    c.seenInLldp     = requireBool(m, contract::K_SEEN_IN_LLDP);
    c.bridgePort       = optionalString(m, contract::K_BRIDGE_PORT);
    c.remoteChassisId  = optionalString(m, contract::K_REMOTE_CHASSIS_ID);
    c.remotePortId     = optionalString(m, contract::K_REMOTE_PORT_ID);
    c.remoteSystemName = optionalString(m, contract::K_REMOTE_SYSTEM_NAME);
    c.neighborIfaces = requireStringSet(m, contract::K_NEIGHBOR_IFACES);
    c.ipv4           = requireStringSet(m, contract::K_IPV4);
    c.ipv6           = requireStringSet(m, contract::K_IPV6);
    return c;
}

contract::ObservationIssues
decodeIssues(const std::map<std::string, std::map<std::string, DBus::Variant>>& issues)
{
    if (issues.size() > interop_contract::ingress::network_observation::kMaxIssues) {
        throw DecodeError(DecodeErrorCode::limit_exceeded,
                          "issues map exceeds ingress limit");
    }

    contract::ObservationIssues decoded;
    for (const auto& [issueCode, fields] : issues) {
        validateKey(issueCode);

        if (fields.size() > interop_contract::ingress::network_observation::kMaxIssueFields) {
            throw DecodeError(DecodeErrorCode::limit_exceeded,
                              "issue '" + issueCode + "' fields map exceeds ingress limit");
        }

        contract::ObservationIssueFields decodedFields;
        for (const auto& [name, value] : fields) {
            validateKey(name);
            if (value.type() != DBus::DataType::STRING) {
                throw DecodeError(DecodeErrorCode::invalid_type,
                                  "issue field '" + name + "' is not a string");
            }
            auto stringValue = value.to_string();
            validateString(stringValue, name.c_str());
            decodedFields.emplace(name, std::move(stringValue));
        }

        // Validate required fields are present.
        static constexpr std::string_view kRequired[] = {
            contract::ISSUE_SEVERITY,
            contract::ISSUE_MESSAGE,
            contract::ISSUE_COMPONENT,
            contract::ISSUE_OPERATION,
            contract::ISSUE_CATEGORY,
            contract::ISSUE_IDENTITY,
        };
        for (const auto& req : kRequired) {
            if (decodedFields.find(std::string(req)) == decodedFields.end()) {
                throw DecodeError(DecodeErrorCode::missing_required_field,
                                  "issue '" + issueCode + "' is missing required field '" +
                                      std::string(req) + "'");
            }
        }

        decoded.emplace(issueCode, std::move(decodedFields));
    }
    return decoded;
}

} // namespace RSCGroup::NetworkObservationDbusCodec
