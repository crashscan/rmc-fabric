#pragma once

// Header-only D-Bus variant map converters for the network-observation contract.
// Consumers must link DBusCxx themselves.

#include "NetworkObservationContracts.hpp"
#include "NetworkObservationTypes.hpp"

#include <DbusVariantMapReader.h>
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

inline LocalInterfaceState fromVariantMapIface(const std::map<std::string, DBus::Variant>& m)
{
    RSCGroup::DbusVariantMapReader r(m);
    LocalInterfaceState s;
    s.ifindex   = r.getInt(std::string(K_IFINDEX).c_str());
    s.ifname    = r.getStr(std::string(K_IFNAME).c_str());
    s.mac       = r.getStr(std::string(K_MAC).c_str());
    s.adminUp   = r.getBool(std::string(K_ADMINUP).c_str());
    s.running   = r.getBool(std::string(K_RUNNING).c_str());
    s.operstate = r.getStr(std::string(K_OPERSTATE).c_str());
    if (m.contains(std::string(K_MASTER)))
        s.masterIfname = r.getStr(std::string(K_MASTER).c_str());
    s.ipv4 = r.getStrSet(std::string(K_IPV4).c_str());
    s.ipv6 = r.getStrSet(std::string(K_IPV6).c_str());
    return s;
}

inline RemoteCandidate fromVariantMapCandidate(const std::map<std::string, DBus::Variant>& m)
{
    RSCGroup::DbusVariantMapReader r(m);
    RemoteCandidate c;
    c.mac            = r.getStr(std::string(K_MAC).c_str());
    c.classification = classificationFromString(r.getStr(std::string(K_CLASSIFICATION).c_str()));
    c.status         = statusFromString(r.getStr(std::string(K_STATUS).c_str()));
    c.seenInFdb      = r.getBool(std::string(K_SEEN_IN_FDB).c_str());
    c.seenInNeigh    = r.getBool(std::string(K_SEEN_IN_NEIGH).c_str());
    c.seenInLldp     = r.getBool(std::string(K_SEEN_IN_LLDP).c_str());
    if (m.contains(std::string(K_BRIDGE_PORT)))
        c.bridgePort       = r.getStr(std::string(K_BRIDGE_PORT).c_str());
    if (m.contains(std::string(K_REMOTE_CHASSIS_ID)))
        c.remoteChassisId  = r.getStr(std::string(K_REMOTE_CHASSIS_ID).c_str());
    if (m.contains(std::string(K_REMOTE_PORT_ID)))
        c.remotePortId     = r.getStr(std::string(K_REMOTE_PORT_ID).c_str());
    if (m.contains(std::string(K_REMOTE_SYSTEM_NAME)))
        c.remoteSystemName = r.getStr(std::string(K_REMOTE_SYSTEM_NAME).c_str());
    c.neighborIfaces = r.getStrSet(std::string(K_NEIGHBOR_IFACES).c_str());
    c.ipv4           = r.getStrSet(std::string(K_IPV4).c_str());
    c.ipv6           = r.getStrSet(std::string(K_IPV6).c_str());
    return c;
}

} // namespace interop_contract::network_observation

