//
// Created by vvass on 21-Jul-26.
//
#include "DbusClient.h"
#include "DbusUtils.h"
#include <dbus-cxx.h>
#include <glog/logging.h>
#include <sigc++/sigc++.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace RSCGroup {

namespace {

constexpr auto DBUS_SERVICE   = "org.rsc.NetworkObservation";
constexpr auto DBUS_PATH      = "/org/rsc/NetworkObservation";
constexpr auto DBUS_INTERFACE = "org.rsc.NetworkObservation";

static CandidateClassification classificationFromString(const std::string& s)
{
    if (s == "Artifact")       return CandidateClassification::Artifact;
    if (s == "LocalSelf")      return CandidateClassification::LocalSelf;
    if (s == "WeakCandidate")   return CandidateClassification::WeakCandidate;
    if (s == "ProbableEndpoint") return CandidateClassification::ProbableEndpoint;
    if (s == "RemoteEndpoint")  return CandidateClassification::RemoteEndpoint;
    if (s == "GatewayLike")    return CandidateClassification::GatewayLike;
    if (s == "TopologyPeer")   return CandidateClassification::TopologyPeer;
    return CandidateClassification::Unknown;
}

static CandidateStatus statusFromString(const std::string& s)
{
    if (s == "Provisional") return CandidateStatus::Provisional;
    if (s == "Confirmed")   return CandidateStatus::Confirmed;
    if (s == "Aged")        return CandidateStatus::Aged;
    if (s == "Expired")     return CandidateStatus::Expired;
    if (s == "Removed")     return CandidateStatus::Removed;
    return CandidateStatus::Provisional;
}

static LocalInterfaceState fromVariantMapIface(const std::map<std::string, DBus::Variant>& m)
{
    DbusVariantMapReader r(m);
    LocalInterfaceState s;
    s.ifindex   = r.getInt("ifindex");
    s.ifname    = r.getStr("ifname");
    s.mac       = r.getStr("mac");
    s.adminUp   = r.getBool("adminUp");
    s.running   = r.getBool("running");
    s.operstate = r.getStr("operstate");
    if (m.contains("master"))
        s.masterIfname = r.getStr("master");
    s.ipv4 = r.getStrSet("ipv4");
    s.ipv6 = r.getStrSet("ipv6");
    return s;
}

static RemoteCandidate fromVariantMapCandidate(const std::map<std::string, DBus::Variant>& m)
{
    DbusVariantMapReader r(m);
    RemoteCandidate c;
    c.mac             = r.getStr("mac");
    c.classification  = classificationFromString(r.getStr("classification"));
    c.status          = statusFromString(r.getStr("status"));
    c.seenInFdb       = r.getBool("seenInFdb");
    c.seenInNeigh     = r.getBool("seenInNeigh");
    c.seenInLldp      = r.getBool("seenInLldp");
    if (m.contains("bridgePort"))   c.bridgePort   = r.getStr("bridgePort");
    if (m.contains("remoteChassisId"))  c.remoteChassisId  = r.getStr("remoteChassisId");
    if (m.contains("remotePortId"))     c.remotePortId     = r.getStr("remotePortId");
    if (m.contains("remoteSystemName")) c.remoteSystemName = r.getStr("remoteSystemName");
    c.ipv4            = r.getStrSet("ipv4");
    c.ipv6            = r.getStrSet("ipv6");
    c.neighborIfaces  = r.getStrSet("neighborIfaces");
    return c;
}

} // anonymous namespace

struct DbusClient::Impl
{
    std::string busType;
    std::shared_ptr<DBus::StandaloneDispatcher> dispatcher;
    std::shared_ptr<DBus::Connection>     connection;
    std::shared_ptr<DBus::ObjectProxy>    proxy;
    std::shared_ptr<DBus::InterfaceProxy> iface;
    bool connected = false;

    std::shared_ptr<DBus::SignalProxy<void()>>            sigLocalStateChanged;
    std::shared_ptr<DBus::SignalProxy<void(std::string)>> sigInterfaceChanged;
    std::shared_ptr<DBus::SignalProxy<void(std::string)>> sigCandidateChanged;
    std::shared_ptr<DBus::SignalProxy<void(std::string)>> sigInterfaceRemoved;
    std::shared_ptr<DBus::SignalProxy<void(std::string)>> sigCandidateRemoved;
    std::shared_ptr<DBus::SignalProxy<void(bool)>>        sigReadyChanged;
};

DbusClient::DbusClient(const std::string& busType)
    : impl_(std::make_unique<Impl>())
{
    impl_->busType = busType;
}

DbusClient::~DbusClient() = default;

bool DbusClient::connect()
{
    try {
        impl_->dispatcher = DBus::StandaloneDispatcher::create();
        impl_->connection = impl_->dispatcher->create_connection(
            impl_->busType == "session" ? DBus::BusType::SESSION : DBus::BusType::SYSTEM);

        impl_->proxy = DBus::ObjectProxy::create(
            impl_->connection, DBUS_SERVICE, DBUS_PATH);
        impl_->iface = impl_->proxy->create_interface(DBUS_INTERFACE);
        impl_->connected = true;
        LOG(INFO) << "DbusClient connected to bus: " << impl_->busType;
        return true;
    } catch (const std::exception& e) {
        LOG(ERROR) << "DbusClient connect failed: " << e.what();
        return false;
    }
}

// --- Query methods ---

LocalNetworkSnapshot DbusClient::getLocalSnapshot()
{
    LocalNetworkSnapshot snap;
    if (!impl_->iface) return snap;
    try {
        auto method = impl_->iface->create_method<
            std::map<std::string, DBus::Variant>()>("GetLocalSnapshot");
        auto raw = (*method)();
        for (const auto& [name, v] : raw) {
            auto varCopy = v;
            snap.interfaces[name] = fromVariantMapIface(varCopy.to_map<std::string, DBus::Variant>());
        }
    } catch (const std::exception& e) {
        LOG(ERROR) << "getLocalSnapshot failed: " << e.what();
    }
    return snap;
}

std::optional<LocalInterfaceState> DbusClient::getInterface(const std::string& ifname)
{
    if (!impl_->iface) return std::nullopt;
    try {
        auto method = impl_->iface->create_method<
            std::map<std::string, DBus::Variant>(std::string)>("GetInterface");
        auto raw = (*method)(ifname);
        if (raw.empty()) return std::nullopt;
        return fromVariantMapIface(raw);
    } catch (const std::exception& e) {
        LOG(ERROR) << "getInterface failed: " << e.what();
        return std::nullopt;
    }
}

std::vector<std::string> DbusClient::getRemoteCandidateMacs()
{
    std::vector<std::string> out;
    if (!impl_->iface) return out;
    try {
        auto method = impl_->iface->create_method<
            std::vector<std::string>()>("GetRemoteCandidateMacs");
        auto raw = (*method)();
        out = std::move(raw);
    } catch (const std::exception& e) {
        LOG(ERROR) << "getRemoteCandidateMacs failed: " << e.what();
    }
    return out;
}

std::optional<RemoteCandidate> DbusClient::getCandidateByMac(const std::string& mac)
{
    if (!impl_->iface) return std::nullopt;
    try {
        auto method = impl_->iface->create_method<
            std::map<std::string, DBus::Variant>(std::string)>("GetCandidateByMac");
        auto raw = (*method)(mac);
        if (raw.empty()) return std::nullopt;
        return fromVariantMapCandidate(raw);
    } catch (const std::exception& e) {
        LOG(ERROR) << "getCandidateByMac failed: " << e.what();
        return std::nullopt;
    }
}

bool DbusClient::getReady()
{
    if (!impl_->iface) return false;
    try {
        auto method = impl_->iface->create_method<bool()>("GetReady");
        return (*method)();
    } catch (const std::exception& e) {
        LOG(ERROR) << "getReady failed: " << e.what();
        return false;
    }
}

std::string DbusClient::getPhase()
{
    if (!impl_->iface) return "unknown";
    try {
        auto method = impl_->iface->create_method<std::string()>("GetPhase");
        return (*method)();
    } catch (const std::exception& e) {
        LOG(ERROR) << "getPhase failed: " << e.what();
        return "unknown";
    }
}

// --- Signal subscriptions ---

void DbusClient::onLocalStateChanged(VoidCallback cb)
{
    if (!impl_->iface) return;
    if (!impl_->sigLocalStateChanged)
        impl_->sigLocalStateChanged = impl_->iface->create_signal<void()>("LocalStateChanged");
    impl_->sigLocalStateChanged->connect(sigc::slot<void()>(cb));
}

void DbusClient::onInterfaceChanged(StringCallback cb)
{
    if (!impl_->iface) return;
    if (!impl_->sigInterfaceChanged)
        impl_->sigInterfaceChanged = impl_->iface->create_signal<void(std::string)>("InterfaceChanged");
    impl_->sigInterfaceChanged->connect(sigc::slot<void(std::string)>(cb));
}

void DbusClient::onCandidateChanged(StringCallback cb)
{
    if (!impl_->iface) return;
    if (!impl_->sigCandidateChanged)
        impl_->sigCandidateChanged = impl_->iface->create_signal<void(std::string)>("CandidateChanged");
    impl_->sigCandidateChanged->connect(sigc::slot<void(std::string)>(cb));
}

void DbusClient::onInterfaceRemoved(StringCallback cb)
{
    if (!impl_->iface) return;
    if (!impl_->sigInterfaceRemoved)
        impl_->sigInterfaceRemoved = impl_->iface->create_signal<void(std::string)>("InterfaceRemoved");
    impl_->sigInterfaceRemoved->connect(sigc::slot<void(std::string)>(cb));
}

void DbusClient::onCandidateRemoved(StringCallback cb)
{
    if (!impl_->iface) return;
    if (!impl_->sigCandidateRemoved)
        impl_->sigCandidateRemoved = impl_->iface->create_signal<void(std::string)>("CandidateRemoved");
    impl_->sigCandidateRemoved->connect(sigc::slot<void(std::string)>(cb));
}

void DbusClient::onReadyChanged(BoolCallback cb)
{
    if (!impl_->iface) return;
    if (!impl_->sigReadyChanged)
        impl_->sigReadyChanged = impl_->iface->create_signal<void(bool)>("ReadyChanged");
    impl_->sigReadyChanged->connect(sigc::slot<void(bool)>(cb));
}

} // namespace RSCGroup