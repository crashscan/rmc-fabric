#include "NetworkObservationDbusAdapter.h"

#include "IObservationQueryService.h"
#include "LocalStateTypes.h"
#include "CandidateTypes.h"

#include <dbus-cxx.h>
#include <glog/logging.h>
#include <sigc++/sigc++.h>

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace RSCGroup {

namespace {

constexpr auto K_IFINDEX            = "ifindex";
constexpr auto K_IFNAME             = "ifname";
constexpr auto K_MAC                = "mac";
constexpr auto K_ADMINUP            = "adminUp";
constexpr auto K_RUNNING            = "running";
constexpr auto K_OPERSTATE          = "operstate";
constexpr auto K_MASTER             = "master";
constexpr auto K_IPV4               = "ipv4";
constexpr auto K_IPV6               = "ipv6";
constexpr auto K_CLASSIFICATION     = "classification";
constexpr auto K_STATUS             = "status";
constexpr auto K_SEEN_IN_FDB        = "seenInFdb";
constexpr auto K_SEEN_IN_NEIGH      = "seenInNeigh";
constexpr auto K_SEEN_IN_LLDP       = "seenInLldp";
constexpr auto K_BRIDGE_PORT        = "bridgePort";
constexpr auto K_REMOTE_CHASSIS_ID  = "remoteChassisId";
constexpr auto K_REMOTE_PORT_ID     = "remotePortId";
constexpr auto K_REMOTE_SYSTEM_NAME = "remoteSystemName";
constexpr auto K_NEIGHBOR_IFACES    = "neighborIfaces";

static std::string classificationToString(CandidateClassification c)
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

static std::string statusToString(CandidateStatus s)
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

static std::map<std::string, DBus::Variant> toVariantMap(const LocalInterfaceState& iface)
{
    std::map<std::string, DBus::Variant> d;
    d[K_IFINDEX]   = DBus::Variant(static_cast<int32_t>(iface.ifindex));
    d[K_IFNAME]    = DBus::Variant(iface.ifname);
    d[K_MAC]       = DBus::Variant(iface.mac);
    d[K_ADMINUP]   = DBus::Variant(iface.adminUp);
    d[K_RUNNING]   = DBus::Variant(iface.running);
    d[K_OPERSTATE] = DBus::Variant(iface.operstate);
    if (iface.masterIfname) d[K_MASTER] = DBus::Variant(*iface.masterIfname);

    std::vector<DBus::Variant> ip4;
    for (const auto& a : iface.ipv4) ip4.emplace_back(a);
    d[K_IPV4] = DBus::Variant(ip4);

    std::vector<DBus::Variant> ip6;
    for (const auto& a : iface.ipv6) ip6.emplace_back(a);
    d[K_IPV6] = DBus::Variant(ip6);

    return d;
}

static std::map<std::string, DBus::Variant> toVariantMap(const RemoteCandidate& c)
{
    std::map<std::string, DBus::Variant> cand;
    cand[K_MAC]            = DBus::Variant(c.mac);
    cand[K_CLASSIFICATION] = DBus::Variant(classificationToString(c.classification));
    cand[K_STATUS]         = DBus::Variant(statusToString(c.status));
    cand[K_SEEN_IN_FDB]    = DBus::Variant(c.seenInFdb);
    cand[K_SEEN_IN_NEIGH]  = DBus::Variant(c.seenInNeigh);
    cand[K_SEEN_IN_LLDP]   = DBus::Variant(c.seenInLldp);
    if (c.bridgePort)       cand[K_BRIDGE_PORT]        = DBus::Variant(*c.bridgePort);
    if (c.remoteChassisId)  cand[K_REMOTE_CHASSIS_ID]  = DBus::Variant(*c.remoteChassisId);
    if (c.remotePortId)     cand[K_REMOTE_PORT_ID]     = DBus::Variant(*c.remotePortId);
    if (c.remoteSystemName) cand[K_REMOTE_SYSTEM_NAME] = DBus::Variant(*c.remoteSystemName);

    std::vector<DBus::Variant> neighIfaces;
    for (const auto& nif : c.neighborIfaces) neighIfaces.emplace_back(nif);
    cand[K_NEIGHBOR_IFACES] = DBus::Variant(neighIfaces);

    std::vector<DBus::Variant> ip4;
    for (const auto& ip : c.ipv4) ip4.emplace_back(ip);
    cand[K_IPV4] = DBus::Variant(ip4);

    std::vector<DBus::Variant> ip6;
    for (const auto& ip : c.ipv6) ip6.emplace_back(ip);
    cand[K_IPV6] = DBus::Variant(ip6);

    return cand;
}

} // anonymous namespace

struct NetworkObservationHandler {
    IObservationQueryService* service = nullptr;

    std::map<std::string, DBus::Variant> GetLocalSnapshot()
    {
        std::map<std::string, DBus::Variant> result;
        if (!service) return result;
        auto snapshot = service->localSnapshot();
        for (const auto& [name, iface] : snapshot.interfaces)
            result[name] = DBus::Variant(toVariantMap(iface));
        return result;
    }

    std::map<std::string, DBus::Variant> GetInterface(std::string ifname)
    {
        if (!service) return {};
        auto iface = service->getInterface(ifname);
        if (!iface) return {};
        return toVariantMap(*iface);
    }

    std::vector<std::string> GetRemoteCandidateMacs()
    {
        std::vector<std::string> result;
        if (!service) return result;
        for (const auto& c : service->remoteCandidates())
            result.push_back(c.mac);
        return result;
    }

    std::map<std::string, DBus::Variant> GetCandidateByMac(std::string mac)
    {
        if (!service) return {};
        auto c = service->getCandidateByMac(mac);
        if (!c) return {};
        return toVariantMap(*c);
    }

    bool GetReady() { return service && service->isReady(); }

    std::string GetPhase()
    {
        if (!service) return "stopped";
        return service->isReady() ? "live" : "initializing";
    }
};

NetworkObservationDbusAdapter::NetworkObservationDbusAdapter()
    : handler_(std::make_shared<NetworkObservationHandler>())
{}

void NetworkObservationDbusAdapter::setService(IObservationQueryService* service)
{
    service_ = service;
    handler_->service = service_;
}

IObservationQueryService* NetworkObservationDbusAdapter::getService() const
{
    return service_;
}

void NetworkObservationDbusAdapter::bind(const std::shared_ptr<DBus::Object>& object,
                                         const std::string& interfaceName)
{
    if (!object) {
        throw std::invalid_argument("NetworkObservationDbusAdapter::bind: object is null");
    }
    createSignals(object, interfaceName);
    bindMethods(object, interfaceName);
}

void NetworkObservationDbusAdapter::onTransportStopping()
{
    service_ = nullptr;
    handler_->service = nullptr;
}

void NetworkObservationDbusAdapter::createSignals(const std::shared_ptr<DBus::Object>& object,
                                                  const std::string& interfaceName)
{
    signalLocalStateChanged_ = createVoidSignal(object, interfaceName, "LocalStateChanged");
    signalInterfaceChanged_  = createStringSignal(object, interfaceName, "InterfaceChanged");
    signalInterfaceRemoved_  = createStringSignal(object, interfaceName, "InterfaceRemoved");
    signalCandidateChanged_  = createStringSignal(object, interfaceName, "CandidateChanged");
    signalCandidateRemoved_  = createStringSignal(object, interfaceName, "CandidateRemoved");
    signalReadyChanged_      = createBoolSignal(object, interfaceName, "ReadyChanged");
}

void NetworkObservationDbusAdapter::bindMethods(const std::shared_ptr<DBus::Object>& object,
                                                const std::string& interfaceName)
{
    auto& h = *handler_;
    object->create_method<std::map<std::string, DBus::Variant>()>(
        interfaceName, "GetLocalSnapshot",
        sigc::mem_fun(h, &NetworkObservationHandler::GetLocalSnapshot));
    object->create_method<std::map<std::string, DBus::Variant>(std::string)>(
        interfaceName, "GetInterface",
        sigc::mem_fun(h, &NetworkObservationHandler::GetInterface));
    object->create_method<std::vector<std::string>()>(
        interfaceName, "GetRemoteCandidateMacs",
        sigc::mem_fun(h, &NetworkObservationHandler::GetRemoteCandidateMacs));
    object->create_method<std::map<std::string, DBus::Variant>(std::string)>(
        interfaceName, "GetCandidateByMac",
        sigc::mem_fun(h, &NetworkObservationHandler::GetCandidateByMac));
    object->create_method<bool()>(
        interfaceName, "GetReady",
        sigc::mem_fun(h, &NetworkObservationHandler::GetReady));
    object->create_method<std::string()>(
        interfaceName, "GetPhase",
        sigc::mem_fun(h, &NetworkObservationHandler::GetPhase));
}

void NetworkObservationDbusAdapter::publishLocalStateChanged()
{
    if (!signalLocalStateChanged_) return;
    signalLocalStateChanged_->emit();
}

void NetworkObservationDbusAdapter::publishInterfaceChanged(const std::string& ifname)
{
    if (!signalInterfaceChanged_) return;
    signalInterfaceChanged_->emit(ifname);
}

void NetworkObservationDbusAdapter::publishInterfaceRemoved(const std::string& ifname)
{
    if (!signalInterfaceRemoved_) return;
    signalInterfaceRemoved_->emit(ifname);
}

void NetworkObservationDbusAdapter::publishCandidateChanged(const std::string& mac)
{
    if (!signalCandidateChanged_) return;
    signalCandidateChanged_->emit(mac);
}

void NetworkObservationDbusAdapter::publishCandidateRemoved(const std::string& mac)
{
    if (!signalCandidateRemoved_) return;
    signalCandidateRemoved_->emit(mac);
}

void NetworkObservationDbusAdapter::publishReadyChanged(bool ready)
{
    if (!signalReadyChanged_) return;
    signalReadyChanged_->emit(ready);
}

} // namespace RSCGroup
