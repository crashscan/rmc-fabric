//
// Created by vvass on 21-Jul-26.
//
#include "DbusClient.h"

#include <interop_contract/network_observation/NetworkObservationContracts.hpp>
#include <interop_contract/network_observation/NetworkObservationVariantMaps.hpp>

#include <dbus-cxx.h>
#include <glog/logging.h>
#include <sigc++/sigc++.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace RSCGroup {

namespace {
using namespace interop_contract::network_observation;
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
            impl_->connection, std::string(SERVICE_NAME), std::string(OBJECT_PATH));
        impl_->iface = impl_->proxy->create_interface(std::string(INTERFACE));
        impl_->connected = true;
        LOG(INFO) << "DbusClient connected to bus: " << impl_->busType;
        return true;
    } catch (const std::exception& e) {
        LOG(ERROR) << "DbusClient connect failed: " << e.what();
        return false;
    }
}

// --- Query methods ---

contract::LocalNetworkSnapshot DbusClient::getLocalSnapshot()
{
    contract::LocalNetworkSnapshot snap;
    if (!impl_->iface) return snap;
    try {
        auto method = impl_->iface->create_method<
            std::map<std::string, DBus::Variant>()>(std::string(METHOD_GET_LOCAL_SNAPSHOT));
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

std::optional<contract::LocalInterfaceState> DbusClient::getInterface(const std::string& ifname)
{
    if (!impl_->iface) return std::nullopt;
    try {
        auto method = impl_->iface->create_method<
            std::map<std::string, DBus::Variant>(std::string)>(std::string(METHOD_GET_INTERFACE));
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
            std::vector<std::string>()>(std::string(METHOD_GET_REMOTE_CANDIDATE_MACS));
        auto raw = (*method)();
        out = std::move(raw);
    } catch (const std::exception& e) {
        LOG(ERROR) << "getRemoteCandidateMacs failed: " << e.what();
    }
    return out;
}

std::optional<contract::RemoteCandidate> DbusClient::getCandidateByMac(const std::string& mac)
{
    if (!impl_->iface) return std::nullopt;
    try {
        auto method = impl_->iface->create_method<
            std::map<std::string, DBus::Variant>(std::string)>(std::string(METHOD_GET_CANDIDATE_BY_MAC));
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
        auto method = impl_->iface->create_method<bool()>(std::string(METHOD_GET_READY));
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
        auto method = impl_->iface->create_method<std::string()>(std::string(METHOD_GET_PHASE));
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
        impl_->sigLocalStateChanged = impl_->iface->create_signal<void()>(
            std::string(SIGNAL_LOCAL_STATE_CHANGED));
    impl_->sigLocalStateChanged->connect(sigc::slot<void()>(cb));
}

void DbusClient::onInterfaceChanged(StringCallback cb)
{
    if (!impl_->iface) return;
    if (!impl_->sigInterfaceChanged)
        impl_->sigInterfaceChanged = impl_->iface->create_signal<void(std::string)>(
            std::string(SIGNAL_INTERFACE_CHANGED));
    impl_->sigInterfaceChanged->connect(sigc::slot<void(std::string)>(cb));
}

void DbusClient::onCandidateChanged(StringCallback cb)
{
    if (!impl_->iface) return;
    if (!impl_->sigCandidateChanged)
        impl_->sigCandidateChanged = impl_->iface->create_signal<void(std::string)>(
            std::string(SIGNAL_CANDIDATE_CHANGED));
    impl_->sigCandidateChanged->connect(sigc::slot<void(std::string)>(cb));
}

void DbusClient::onInterfaceRemoved(StringCallback cb)
{
    if (!impl_->iface) return;
    if (!impl_->sigInterfaceRemoved)
        impl_->sigInterfaceRemoved = impl_->iface->create_signal<void(std::string)>(
            std::string(SIGNAL_INTERFACE_REMOVED));
    impl_->sigInterfaceRemoved->connect(sigc::slot<void(std::string)>(cb));
}

void DbusClient::onCandidateRemoved(StringCallback cb)
{
    if (!impl_->iface) return;
    if (!impl_->sigCandidateRemoved)
        impl_->sigCandidateRemoved = impl_->iface->create_signal<void(std::string)>(
            std::string(SIGNAL_CANDIDATE_REMOVED));
    impl_->sigCandidateRemoved->connect(sigc::slot<void(std::string)>(cb));
}

void DbusClient::onReadyChanged(BoolCallback cb)
{
    if (!impl_->iface) return;
    if (!impl_->sigReadyChanged)
        impl_->sigReadyChanged = impl_->iface->create_signal<void(bool)>(
            std::string(SIGNAL_READY_CHANGED));
    impl_->sigReadyChanged->connect(sigc::slot<void(bool)>(cb));
}

} // namespace RSCGroup