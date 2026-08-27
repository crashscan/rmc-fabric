//
// Created by vvass on 21-Jul-26.
//
#include "DbusTransport.h"

#include "IObservationQueryService.h"
#include "LocalStateTypes.h"
#include "CandidateTypes.h"
#include <dbus-cxx.h>
#include <glog/logging.h>
#include <sigc++/sigc++.h>

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace RSCGroup {

namespace {

constexpr auto DBUS_SERVICE   = "org.rsc.NetworkObservation";
constexpr auto DBUS_PATH      = "/org/rsc/NetworkObservation";
constexpr auto DBUS_INTERFACE = "org.rsc.NetworkObservation";

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
        case CandidateClassification::Artifact:        return "Artifact";
        case CandidateClassification::LocalSelf:       return "LocalSelf";
        case CandidateClassification::WeakCandidate:   return "WeakCandidate";
        case CandidateClassification::ProbableEndpoint: return "ProbableEndpoint";
        case CandidateClassification::RemoteEndpoint:  return "RemoteEndpoint";
        case CandidateClassification::GatewayLike:     return "GatewayLike";
        case CandidateClassification::TopologyPeer:    return "TopologyPeer";
        case CandidateClassification::Unknown:         return "Unknown";
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
    cand[K_MAC]                = DBus::Variant(c.mac);
    cand[K_CLASSIFICATION]     = DBus::Variant(classificationToString(c.classification));
    cand[K_STATUS]             = DBus::Variant(statusToString(c.status));
    cand[K_SEEN_IN_FDB]        = DBus::Variant(c.seenInFdb);
    cand[K_SEEN_IN_NEIGH]      = DBus::Variant(c.seenInNeigh);
    cand[K_SEEN_IN_LLDP]       = DBus::Variant(c.seenInLldp);
    if (c.bridgePort)          cand[K_BRIDGE_PORT] = DBus::Variant(*c.bridgePort);
    if (c.remoteChassisId)     cand[K_REMOTE_CHASSIS_ID]  = DBus::Variant(*c.remoteChassisId);
    if (c.remotePortId)        cand[K_REMOTE_PORT_ID]     = DBus::Variant(*c.remotePortId);
    if (c.remoteSystemName)    cand[K_REMOTE_SYSTEM_NAME] = DBus::Variant(*c.remoteSystemName);

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

struct NetworkObservationHandler
{
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
        for (const auto& c : service->remoteCandidates()) {
            result.push_back(c.mac);
        }
        return result;
    }

    /*std::vector<std::map<std::string, DBus::Variant>> GetRemoteCandidates()
    {
        std::vector<std::map<std::string, DBus::Variant>> result;
        if (!service) return result;
        for (const auto& c : service->remoteCandidates())
            result.push_back(toVariantMap(c));
        return result;
    }*/

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

} // anonymous namespace

struct DbusTransport::Impl
{
    std::string busType;
    std::shared_ptr<DBus::StandaloneDispatcher> dispatcher;
    std::shared_ptr<DBus::Connection> connection;
    std::shared_ptr<DBus::Object> object;
    NetworkObservationHandler handler;

    std::shared_ptr<DBus::Signal<void()>>                     signalLocalStateChanged;
    std::shared_ptr<DBus::Signal<void(std::string)>>          signalInterfaceChanged;
    std::shared_ptr<DBus::Signal<void(std::string)>>          signalInterfaceRemoved;
    std::shared_ptr<DBus::Signal<void(std::string)>>          signalCandidateChanged;
    std::shared_ptr<DBus::Signal<void(std::string)>>          signalCandidateRemoved;
    std::shared_ptr<DBus::Signal<void(bool)>>                 signalReadyChanged;

    std::atomic<bool> running{false};
};

DbusTransport::DbusTransport(const std::string& busType)
    : impl_(std::make_unique<Impl>()) { impl_->busType = busType; }

DbusTransport::~DbusTransport() = default;

void DbusTransport::setQueryProvider(IObservationQueryService* provider)
{
    impl_->handler.service = provider;
}

bool DbusTransport::start()
{
    if (!impl_->handler.service) {
        LOG(ERROR) << "DbusTransport query provider not set";
        return false;
    }
    try {
        impl_->dispatcher = DBus::StandaloneDispatcher::create();
        impl_->connection = impl_->dispatcher->create_connection(
            impl_->busType == "session" ? DBus::BusType::SESSION : DBus::BusType::SYSTEM);

        impl_->connection->request_name(DBUS_SERVICE);
        impl_->object = impl_->connection->create_object(DBUS_PATH);
        impl_->connection->register_object(impl_->object);

        auto& obj = impl_->object;
        auto& h   = impl_->handler;

        obj->create_method<std::map<std::string, DBus::Variant>()>(
            DBUS_INTERFACE, "GetLocalSnapshot", sigc::mem_fun(h, &NetworkObservationHandler::GetLocalSnapshot));
        obj->create_method<std::map<std::string, DBus::Variant>(std::string)>(
            DBUS_INTERFACE, "GetInterface", sigc::mem_fun(h, &NetworkObservationHandler::GetInterface));
        //obj->create_method<std::vector<std::map<std::string, DBus::Variant>>()>(
//            DBUS_INTERFACE, "GetRemoteCandidates", sigc::mem_fun(h, &NetworkObservationHandler::GetRemoteCandidates));
        obj->create_method<std::vector<std::string>()>(
            DBUS_INTERFACE, "GetRemoteCandidateMacs", sigc::mem_fun(h, &NetworkObservationHandler::GetRemoteCandidateMacs));

        obj->create_method<std::map<std::string, DBus::Variant>(std::string)>(
            DBUS_INTERFACE, "GetCandidateByMac", sigc::mem_fun(h, &NetworkObservationHandler::GetCandidateByMac));
        obj->create_method<bool()>(
            DBUS_INTERFACE, "GetReady", sigc::mem_fun(h, &NetworkObservationHandler::GetReady));
        obj->create_method<std::string()>(
            DBUS_INTERFACE, "GetPhase", sigc::mem_fun(h, &NetworkObservationHandler::GetPhase));

        impl_->signalLocalStateChanged = obj->create_signal<void()>(DBUS_INTERFACE, "LocalStateChanged");
        impl_->signalInterfaceChanged  = obj->create_signal<void(std::string)>(DBUS_INTERFACE, "InterfaceChanged");
        impl_->signalInterfaceRemoved  = obj->create_signal<void(std::string)>(DBUS_INTERFACE, "InterfaceRemoved");
        impl_->signalCandidateChanged  = obj->create_signal<void(std::string)>(DBUS_INTERFACE, "CandidateChanged");
        impl_->signalCandidateRemoved  = obj->create_signal<void(std::string)>(DBUS_INTERFACE, "CandidateRemoved");
        impl_->signalReadyChanged      = obj->create_signal<void(bool)>(DBUS_INTERFACE, "ReadyChanged");

        impl_->running.store(true, std::memory_order_release);
        LOG(INFO) << "DbusTransport started on bus: " << impl_->busType;
        return true;
    } catch (const std::exception& e) {
        LOG(ERROR) << "DbusTransport start failed: " << e.what();
        return false;
    }
}

void DbusTransport::stop()
{
    if (impl_->object && impl_->connection)
        impl_->connection->unregister_object(DBUS_PATH);
    impl_->object.reset();
    impl_->connection.reset();
    impl_->dispatcher.reset();
    impl_->running.store(false, std::memory_order_release);
}

void DbusTransport::publishLocalStateChanged()
{
    if (!impl_->running.load(std::memory_order_acquire) || !impl_->signalLocalStateChanged) return;
    impl_->signalLocalStateChanged->emit();
}

void DbusTransport::publishInterfaceChanged(const std::string& ifname)
{
    if (!impl_->running.load(std::memory_order_acquire) || !impl_->signalInterfaceChanged) return;
    impl_->signalInterfaceChanged->emit(ifname);
}

void DbusTransport::publishInterfaceRemoved(const std::string& ifname)
{
    if (!impl_->running.load(std::memory_order_acquire) || !impl_->signalInterfaceRemoved) return;
    impl_->signalInterfaceRemoved->emit(ifname);
}

void DbusTransport::publishCandidateChanged(const std::string& mac)
{
    if (!impl_->running.load(std::memory_order_acquire) || !impl_->signalCandidateChanged) return;
    impl_->signalCandidateChanged->emit(mac);
}

void DbusTransport::publishCandidateRemoved(const std::string& mac)
{
    if (!impl_->running.load(std::memory_order_acquire) || !impl_->signalCandidateRemoved) return;
    impl_->signalCandidateRemoved->emit(mac);
}

void DbusTransport::publishReadyChanged(bool ready)
{
    if (!impl_->running.load(std::memory_order_acquire) || !impl_->signalReadyChanged) return;
    impl_->signalReadyChanged->emit(ready);
}

} // namespace RSCGroup