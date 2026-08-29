#pragma once

// Header-only D-Bus variant map converters for the network-observation contract.
// Consumers must link DBusCxx themselves.

#include "NetworkObservationContracts.hpp"
#include "NetworkObservationTypes.hpp"

#include <dbus-cxx.h>

#include <map>
#include <string>
#include <vector>

namespace interop_contract::network_observation {

// ---------------------------------------------------------------------------
// Enum ↔ string helpers
// ---------------------------------------------------------------------------

inline std::string classificationToString(CandidateClassification c)
{
    switch (c) {
        case CandidateClassification::Artifact:         return "Artifact";
        case CandidateClassification::LocalSelf:        return "LocalSelf";
        case CandidateClassification::WeakCandidate:    return "WeakCandidate";
        case CandidateClassification::ProbableEndpoint: return "ProbableEndpoint";
        case CandidateClassification::RemoteEndpoint:   return "RemoteEndpoint";
        case CandidateClassification::GatewayLike:      return "GatewayLike";
        case CandidateClassification::TopologyPeer:     return "TopologyPeer";
        case CandidateClassification::Unknown:          return "Unknown";
    }
    return "Unknown";
}

inline CandidateClassification classificationFromString(const std::string& s)
{
    if (s == "Artifact")         return CandidateClassification::Artifact;
    if (s == "LocalSelf")        return CandidateClassification::LocalSelf;
    if (s == "WeakCandidate")    return CandidateClassification::WeakCandidate;
    if (s == "ProbableEndpoint") return CandidateClassification::ProbableEndpoint;
    if (s == "RemoteEndpoint")   return CandidateClassification::RemoteEndpoint;
    if (s == "GatewayLike")      return CandidateClassification::GatewayLike;
    if (s == "TopologyPeer")     return CandidateClassification::TopologyPeer;
    return CandidateClassification::Unknown;
}

inline std::string statusToString(CandidateStatus s)
{
    switch (s) {
        case CandidateStatus::Provisional: return "Provisional";
        case CandidateStatus::Confirmed:   return "Confirmed";
        case CandidateStatus::Aged:        return "Aged";
        case CandidateStatus::Expired:     return "Expired";
        case CandidateStatus::Removed:     return "Removed";
    }
    return "Unknown";
}

inline CandidateStatus statusFromString(const std::string& s)
{
    if (s == "Provisional") return CandidateStatus::Provisional;
    if (s == "Confirmed")   return CandidateStatus::Confirmed;
    if (s == "Aged")        return CandidateStatus::Aged;
    if (s == "Expired")     return CandidateStatus::Expired;
    if (s == "Removed")     return CandidateStatus::Removed;
    return CandidateStatus::Provisional;
}

// ---------------------------------------------------------------------------
// toVariantMap — serialize domain types to D-Bus a{sv}
// ---------------------------------------------------------------------------

inline std::map<std::string, DBus::Variant> toVariantMap(const LocalInterfaceState& iface)
{
    std::map<std::string, DBus::Variant> d;
    d[std::string(K_IFINDEX)]   = DBus::Variant(static_cast<int32_t>(iface.ifindex));
    d[std::string(K_IFNAME)]    = DBus::Variant(iface.ifname);
    d[std::string(K_MAC)]       = DBus::Variant(iface.mac);
    d[std::string(K_ADMINUP)]   = DBus::Variant(iface.adminUp);
    d[std::string(K_RUNNING)]   = DBus::Variant(iface.running);
    d[std::string(K_OPERSTATE)] = DBus::Variant(iface.operstate);
    if (iface.masterIfname)
        d[std::string(K_MASTER)] = DBus::Variant(*iface.masterIfname);

    std::vector<DBus::Variant> ip4;
    for (const auto& a : iface.ipv4) ip4.emplace_back(a);
    d[std::string(K_IPV4)] = DBus::Variant(ip4);

    std::vector<DBus::Variant> ip6;
    for (const auto& a : iface.ipv6) ip6.emplace_back(a);
    d[std::string(K_IPV6)] = DBus::Variant(ip6);

    return d;
}

inline std::map<std::string, DBus::Variant> toVariantMap(const RemoteCandidate& c)
{
    std::map<std::string, DBus::Variant> cand;
    cand[std::string(K_MAC)]            = DBus::Variant(c.mac);
    cand[std::string(K_CLASSIFICATION)] = DBus::Variant(classificationToString(c.classification));
    cand[std::string(K_STATUS)]         = DBus::Variant(statusToString(c.status));
    cand[std::string(K_SEEN_IN_FDB)]    = DBus::Variant(c.seenInFdb);
    cand[std::string(K_SEEN_IN_NEIGH)]  = DBus::Variant(c.seenInNeigh);
    cand[std::string(K_SEEN_IN_LLDP)]   = DBus::Variant(c.seenInLldp);
    if (c.bridgePort)
        cand[std::string(K_BRIDGE_PORT)]        = DBus::Variant(*c.bridgePort);
    if (c.remoteChassisId)
        cand[std::string(K_REMOTE_CHASSIS_ID)]  = DBus::Variant(*c.remoteChassisId);
    if (c.remotePortId)
        cand[std::string(K_REMOTE_PORT_ID)]     = DBus::Variant(*c.remotePortId);
    if (c.remoteSystemName)
        cand[std::string(K_REMOTE_SYSTEM_NAME)] = DBus::Variant(*c.remoteSystemName);

    std::vector<DBus::Variant> neighIfaces;
    for (const auto& nif : c.neighborIfaces) neighIfaces.emplace_back(nif);
    cand[std::string(K_NEIGHBOR_IFACES)] = DBus::Variant(neighIfaces);

    std::vector<DBus::Variant> ip4;
    for (const auto& ip : c.ipv4) ip4.emplace_back(ip);
    cand[std::string(K_IPV4)] = DBus::Variant(ip4);

    std::vector<DBus::Variant> ip6;
    for (const auto& ip : c.ipv6) ip6.emplace_back(ip);
    cand[std::string(K_IPV6)] = DBus::Variant(ip6);

    return cand;
}

// ---------------------------------------------------------------------------
// fromVariantMap — deserialize D-Bus a{sv} to domain types
// ---------------------------------------------------------------------------

/// Helper: read a string-keyed variant map using a DbusVariantMapReader-style API.
/// Avoids pulling in DbusVariantMapReader for the contract header.
namespace detail {

inline int getInt(const std::map<std::string, DBus::Variant>& m, std::string_view k)
{
    auto key = std::string(k);
    return m.contains(key) ? static_cast<int>(m.at(key).to_int32()) : 0;
}

inline std::string getStr(const std::map<std::string, DBus::Variant>& m, std::string_view k)
{
    auto key = std::string(k);
    return m.contains(key) ? m.at(key).to_string() : std::string{};
}

inline bool getBool(const std::map<std::string, DBus::Variant>& m, std::string_view k)
{
    auto key = std::string(k);
    return m.contains(key) && m.at(key).to_bool();
}

inline std::set<std::string> getStrSet(const std::map<std::string, DBus::Variant>& m,
                                       std::string_view k)
{
    auto key = std::string(k);
    if (!m.contains(key)) return {};
    std::set<std::string> out;
    auto varCopy = m.at(key);
    for (const auto& v : varCopy.to_vector<DBus::Variant>())
        out.insert(v.to_string());
    return out;
}

} // namespace detail

inline LocalInterfaceState fromVariantMapIface(const std::map<std::string, DBus::Variant>& m)
{
    LocalInterfaceState s;
    s.ifindex   = detail::getInt(m, K_IFINDEX);
    s.ifname    = detail::getStr(m, K_IFNAME);
    s.mac       = detail::getStr(m, K_MAC);
    s.adminUp   = detail::getBool(m, K_ADMINUP);
    s.running   = detail::getBool(m, K_RUNNING);
    s.operstate = detail::getStr(m, K_OPERSTATE);
    if (m.contains(std::string(K_MASTER)))
        s.masterIfname = detail::getStr(m, K_MASTER);
    s.ipv4 = detail::getStrSet(m, K_IPV4);
    s.ipv6 = detail::getStrSet(m, K_IPV6);
    return s;
}

inline RemoteCandidate fromVariantMapCandidate(const std::map<std::string, DBus::Variant>& m)
{
    RemoteCandidate c;
    c.mac             = detail::getStr(m, K_MAC);
    c.classification  = classificationFromString(detail::getStr(m, K_CLASSIFICATION));
    c.status          = statusFromString(detail::getStr(m, K_STATUS));
    c.seenInFdb       = detail::getBool(m, K_SEEN_IN_FDB);
    c.seenInNeigh     = detail::getBool(m, K_SEEN_IN_NEIGH);
    c.seenInLldp      = detail::getBool(m, K_SEEN_IN_LLDP);
    if (m.contains(std::string(K_BRIDGE_PORT)))
        c.bridgePort       = detail::getStr(m, K_BRIDGE_PORT);
    if (m.contains(std::string(K_REMOTE_CHASSIS_ID)))
        c.remoteChassisId  = detail::getStr(m, K_REMOTE_CHASSIS_ID);
    if (m.contains(std::string(K_REMOTE_PORT_ID)))
        c.remotePortId     = detail::getStr(m, K_REMOTE_PORT_ID);
    if (m.contains(std::string(K_REMOTE_SYSTEM_NAME)))
        c.remoteSystemName = detail::getStr(m, K_REMOTE_SYSTEM_NAME);
    c.neighborIfaces  = detail::getStrSet(m, K_NEIGHBOR_IFACES);
    c.ipv4            = detail::getStrSet(m, K_IPV4);
    c.ipv6            = detail::getStrSet(m, K_IPV6);
    return c;
}

} // namespace interop_contract::network_observation
