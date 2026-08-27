#include "InventoryClient.h"

#include <inventory_transport/inventory/InventoryDbusCodec.h>

#include <dbus-cxx.h>
#include <glog/logging.h>
#include <sigc++/sigc++.h>

#include <chrono>
#include <thread>

namespace RSCGroup {
namespace {

using namespace interop_contract::inventory;

} // namespace

struct InventoryClient::Impl
{
    std::shared_ptr<DBus::Connection>     connection;
    std::shared_ptr<DBus::ObjectProxy>    proxy;
    std::shared_ptr<DBus::InterfaceProxy> iface;

    std::string serviceName;
    std::string objectPath;
    std::string interfaceName;

    std::shared_ptr<DBus::SignalProxy<void(std::string)>> sigInventoryChanged;
    std::shared_ptr<DBus::SignalProxy<void(std::string)>> sigSourceStateChanged;
    std::shared_ptr<DBus::SignalProxy<void(bool)>>        sigReadyChanged;
};

InventoryClient::InventoryClient(std::shared_ptr<DBus::Connection> connection,
                                 std::string serviceName,
                                 std::string objectPath,
                                 std::string interfaceName)
    : impl_(std::make_unique<Impl>())
{
    if (!connection) {
        throw std::invalid_argument("InventoryClient: connection is null");
    }

    impl_->connection    = std::move(connection);
    impl_->serviceName   = std::move(serviceName);
    impl_->objectPath    = std::move(objectPath);
    impl_->interfaceName = std::move(interfaceName);

    impl_->proxy = impl_->connection->create_object_proxy(
        impl_->serviceName,
        impl_->objectPath,
        DBus::ThreadForCalling::DispatcherThread);

    if (!impl_->proxy) {
        throw std::runtime_error("InventoryClient: failed to create object proxy");
    }

    impl_->iface = impl_->proxy->create_interface(impl_->interfaceName);
    if (!impl_->iface) {
        throw std::runtime_error("InventoryClient: failed to create interface proxy");
    }
}

InventoryClient::~InventoryClient()
{
    if (!impl_) {
        return;
    }

    impl_->sigInventoryChanged.reset();
    impl_->sigSourceStateChanged.reset();
    impl_->sigReadyChanged.reset();
    impl_->iface.reset();
    impl_->proxy.reset();
    impl_->connection.reset();
}

InventorySnapshot InventoryClient::getIdentity() const
{
    if (!impl_->iface) return {};
    try {
        auto method = impl_->iface->create_method<
            std::map<std::string, DBus::Variant>()>(std::string(METHOD_GET_IDENTITY));
        return InventoryDbusCodec::decodeSnapshot((*method)());
    } catch (const std::exception& e) {
        LOG(ERROR) << "InventoryClient::getIdentity failed: " << e.what();
        return {};
    }
}

InventoryFields InventoryClient::getField(const std::string& fieldName) const
{
    if (!impl_->iface) return {};
    try {
        auto method = impl_->iface->create_method<
            std::map<std::string, DBus::Variant>(std::string)>(std::string(METHOD_GET_FIELD));
        return InventoryDbusCodec::decodeFields((*method)(fieldName));
    } catch (const std::exception& e) {
        LOG(ERROR) << "InventoryClient::getField failed: " << e.what();
        return {};
    }
}

interop_contract::inventory::SourceStateMap InventoryClient::getSourceStates() const
{
    if (!impl_->iface) return {};
    try {
        auto method = impl_->iface->create_method<
            std::map<std::string, std::map<std::string, DBus::Variant>>()>(
                std::string(METHOD_GET_SOURCE_STATES));
        return InventoryDbusCodec::decodeSourceStates((*method)());
    } catch (const std::exception& e) {
        LOG(ERROR) << "InventoryClient::getSourceStates failed: " << e.what();
        return {};
    }
}

bool InventoryClient::getReady() const
{
    if (!impl_->iface) return false;
    try {
        auto method = impl_->iface->create_method<bool()>(std::string(METHOD_GET_READY));
        return (*method)();
    } catch (const std::exception& e) {
        LOG(ERROR) << "InventoryClient::getReady failed: " << e.what();
        return false;
    }
}

std::string InventoryClient::getPhase() const
{
    if (!impl_->iface) return "unknown";
    try {
        auto method = impl_->iface->create_method<std::string()>(std::string(METHOD_GET_PHASE));
        return (*method)();
    } catch (const std::exception& e) {
        LOG(ERROR) << "InventoryClient::getPhase failed: " << e.what();
        return "unknown";
    }
}

uint64_t InventoryClient::getVersion() const
{
    if (!impl_->iface) return 0;
    try {
        auto method = impl_->iface->create_method<uint64_t()>(std::string(METHOD_GET_VERSION));
        return (*method)();
    } catch (const std::exception& e) {
        LOG(ERROR) << "InventoryClient::getVersion failed: " << e.what();
        return 0;
    }
}

interop_contract::inventory::InventoryIssues InventoryClient::getIssues() const
{
    if (!impl_->iface) return {};
    try {
        auto method = impl_->iface->create_method<
            std::map<std::string, std::map<std::string, DBus::Variant>>()>(
                std::string(METHOD_GET_ISSUES));
        return InventoryDbusCodec::decodeIssues((*method)());
    } catch (const std::exception& e) {
        LOG(ERROR) << "InventoryClient::getIssues failed: " << e.what();
        return {};
    }
}

void InventoryClient::refresh() const
{
    if (!impl_->iface) return;
    try {
        auto method = impl_->iface->create_method<void()>(std::string(METHOD_REFRESH));
        (*method)();
    } catch (const std::exception& e) {
        LOG(ERROR) << "InventoryClient::refresh failed: " << e.what();
    }
}

void InventoryClient::onInventoryChanged(StringCallback cb)
{
    if (!impl_->iface) return;
    if (!impl_->sigInventoryChanged) {
        impl_->sigInventoryChanged = impl_->iface->create_signal<void(std::string)>(
            std::string(SIGNAL_INVENTORY_CHANGED));
    }
    impl_->sigInventoryChanged->connect(sigc::slot<void(std::string)>(std::move(cb)));
}

void InventoryClient::onSourceStateChanged(StringCallback cb)
{
    if (!impl_->iface) return;
    if (!impl_->sigSourceStateChanged) {
        impl_->sigSourceStateChanged = impl_->iface->create_signal<void(std::string)>(
            std::string(SIGNAL_SOURCE_STATE_CHANGED));
    }
    impl_->sigSourceStateChanged->connect(sigc::slot<void(std::string)>(std::move(cb)));
}

void InventoryClient::onReadyChanged(BoolCallback cb)
{
    if (!impl_->iface) return;
    if (!impl_->sigReadyChanged) {
        impl_->sigReadyChanged = impl_->iface->create_signal<void(bool)>(
            std::string(SIGNAL_READY_CHANGED));
    }
    impl_->sigReadyChanged->connect(sigc::slot<void(bool)>(std::move(cb)));
}

bool InventoryClient::waitReady(std::chrono::milliseconds timeout) const
{
    constexpr auto kPollInterval = std::chrono::milliseconds(200);
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline) {
        if (getReady()) return true;
        std::this_thread::sleep_for(kPollInterval);
    }
    return getReady();
}

} // namespace RSCGroup
