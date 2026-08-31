#include "NetworkObservationDbusCodec.h"

#include <DbusVariantMapReader.h>
#include <interop_contract/network_observation/NetworkObservationEnumStrings.hpp>

#include <dbus-cxx.h>

#include <map>
#include <string>
#include <vector>

namespace RSCGroup::NetworkObservationDbusCodec {

namespace contract = interop_contract::network_observation;

std::map<std::string, DBus::Variant>
toVariantMap(const contract::LocalInterfaceState& iface)
{
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

std::map<std::string, DBus::Variant>
toVariantMap(const contract::RemoteCandidate& c)
{
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

contract::LocalInterfaceState
fromVariantMapIface(const std::map<std::string, DBus::Variant>& m)
{
    RSCGroup::DbusVariantMapReader r(m);
    contract::LocalInterfaceState s;
    s.ifindex   = r.getInt(std::string(contract::K_IFINDEX).c_str());
    s.ifname    = r.getStr(std::string(contract::K_IFNAME).c_str());
    s.mac       = r.getStr(std::string(contract::K_MAC).c_str());
    s.adminUp   = r.getBool(std::string(contract::K_ADMINUP).c_str());
    s.running   = r.getBool(std::string(contract::K_RUNNING).c_str());
    s.operstate = r.getStr(std::string(contract::K_OPERSTATE).c_str());
    if (m.contains(std::string(contract::K_MASTER)))
        s.masterIfname = r.getStr(std::string(contract::K_MASTER).c_str());
    s.ipv4 = r.getStrSet(std::string(contract::K_IPV4).c_str());
    s.ipv6 = r.getStrSet(std::string(contract::K_IPV6).c_str());
    return s;
}

contract::RemoteCandidate
fromVariantMapCandidate(const std::map<std::string, DBus::Variant>& m)
{
    RSCGroup::DbusVariantMapReader r(m);
    contract::RemoteCandidate c;
    c.mac            = r.getStr(std::string(contract::K_MAC).c_str());
    c.classification = contract::classificationFromString(r.getStr(std::string(contract::K_CLASSIFICATION).c_str()));
    c.status         = contract::statusFromString(r.getStr(std::string(contract::K_STATUS).c_str()));
    c.seenInFdb      = r.getBool(std::string(contract::K_SEEN_IN_FDB).c_str());
    c.seenInNeigh    = r.getBool(std::string(contract::K_SEEN_IN_NEIGH).c_str());
    c.seenInLldp     = r.getBool(std::string(contract::K_SEEN_IN_LLDP).c_str());
    if (m.contains(std::string(contract::K_BRIDGE_PORT)))
        c.bridgePort       = r.getStr(std::string(contract::K_BRIDGE_PORT).c_str());
    if (m.contains(std::string(contract::K_REMOTE_CHASSIS_ID)))
        c.remoteChassisId  = r.getStr(std::string(contract::K_REMOTE_CHASSIS_ID).c_str());
    if (m.contains(std::string(contract::K_REMOTE_PORT_ID)))
        c.remotePortId     = r.getStr(std::string(contract::K_REMOTE_PORT_ID).c_str());
    if (m.contains(std::string(contract::K_REMOTE_SYSTEM_NAME)))
        c.remoteSystemName = r.getStr(std::string(contract::K_REMOTE_SYSTEM_NAME).c_str());
    c.neighborIfaces = r.getStrSet(std::string(contract::K_NEIGHBOR_IFACES).c_str());
    c.ipv4           = r.getStrSet(std::string(contract::K_IPV4).c_str());
    c.ipv6           = r.getStrSet(std::string(contract::K_IPV6).c_str());
    return c;
}

} // namespace RSCGroup::NetworkObservationDbusCodec
