#include "InventoryClient.h"

#include "InventoryDbusCodec.h"

#include <DecodeError.hpp>

#include <dbus-cxx.h>
#include <glog/logging.h>
#include <sigc++/sigc++.h>

#include <chrono>
#include <thread>
#include <utility>

namespace RSCGroup {
namespace {

using namespace interop_contract::inventory;

interop_contract::ClientError classifyDbusError(const DBus::Error& error)
{
    using interop_contract::ClientError;
    using interop_contract::ClientErrorCode;

    if (dynamic_cast<const DBus::ErrorTimeout*>(&error) ||
        dynamic_cast<const DBus::ErrorTimedOut*>(&error) ||
        dynamic_cast<const DBus::ErrorNoReply*>(&error)) {
        return {ClientErrorCode::timeout, error.what()};
    }

    if (dynamic_cast<const DBus::ErrorServiceUnknown*>(&error) ||
        dynamic_cast<const DBus::ErrorNameHasNoOwner*>(&error) ||
        dynamic_cast<const DBus::ErrorNoConnection*>(&error) ||
        dynamic_cast<const DBus::ErrorNoServer*>(&error) ||
        dynamic_cast<const DBus::ErrorUnknownObject*>(&error) ||
        dynamic_cast<const DBus::ErrorUnknownInterface*>(&error) ||
        dynamic_cast<const DBus::ErrorUnknownMethod*>(&error)) {
        return {ClientErrorCode::service_unavailable, error.what()};
    }

    if (dynamic_cast<const DBus::ErrorUnexpectedResponse*>(&error) ||
        dynamic_cast<const DBus::ErrorInvalidReturn*>(&error) ||
        dynamic_cast<const DBus::ErrorInvalidSignature*>(&error) ||
        dynamic_cast<const DBus::ErrorInvalidMessageType*>(&error) ||
        dynamic_cast<const DBus::ErrorBadVariantCast*>(&error)) {
        return {ClientErrorCode::invalid_response, error.what()};
    }

    return {ClientErrorCode::transport_error, error.what()};
}

template <class T, class Fn>
interop_contract::ClientResult<T> invokeQuery(const char* operation, Fn&& fn)
{
    try {
        return fn();
    } catch (const interop_contract::DecodeError& error) {
        LOG(ERROR) << operation << " failed: " << error.what();
        return interop_contract::ClientError{
            interop_contract::ClientErrorCode::decode_error,
            error.what(),
        };
    } catch (const DBus::Error& error) {
        const auto mapped = classifyDbusError(error);
        LOG(ERROR) << operation << " failed: " << mapped.message;
        return mapped;
    } catch (const std::exception& error) {
        LOG(ERROR) << operation << " failed: " << error.what();
        return interop_contract::ClientError{
            interop_contract::ClientErrorCode::transport_error,
            error.what(),
        };
    }
}

DBus::BusType toBusType(const std::string& busType)
{
    return busType == "session" ? DBus::BusType::SESSION : DBus::BusType::SYSTEM;
}

} // namespace

struct InventoryClient::Impl
{
    std::shared_ptr<DBus::StandaloneDispatcher> dispatcher;
    std::shared_ptr<DBus::Connection> connection;
    std::shared_ptr<DBus::ObjectProxy> proxy;
    std::shared_ptr<DBus::InterfaceProxy> iface;

    std::string serviceName;
    std::string objectPath;
    std::string interfaceName;

    std::shared_ptr<DBus::SignalProxy<void(std::string)>> sigInventoryChanged;
    std::shared_ptr<DBus::SignalProxy<void(std::string)>> sigSourceStateChanged;
    std::shared_ptr<DBus::SignalProxy<void(bool)>> sigReadyChanged;
};

InventoryClient::InventoryClient(std::shared_ptr<void> connection,
                                 std::string serviceName,
                                 std::string objectPath,
                                 std::string interfaceName)
    : impl_(std::make_unique<Impl>())
{
    impl_->serviceName = std::move(serviceName);
    impl_->objectPath = std::move(objectPath);
    impl_->interfaceName = std::move(interfaceName);

    if (connection) {
        impl_->connection = std::static_pointer_cast<DBus::Connection>(std::move(connection));
    } else {
        impl_->dispatcher = DBus::StandaloneDispatcher::create();
        impl_->connection = impl_->dispatcher->create_connection(DBus::BusType::SYSTEM);
    }

    if (!impl_->connection) {
        throw std::invalid_argument("InventoryClient: connection is null");
    }

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

InventoryClient::InventoryClient(std::string busType,
                                 std::string serviceName,
                                 std::string objectPath,
                                 std::string interfaceName)
    : impl_(std::make_unique<Impl>())
{
    impl_->serviceName = std::move(serviceName);
    impl_->objectPath = std::move(objectPath);
    impl_->interfaceName = std::move(interfaceName);
    impl_->dispatcher = DBus::StandaloneDispatcher::create();
    impl_->connection = impl_->dispatcher->create_connection(toBusType(busType));
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
    impl_->dispatcher.reset();
}

interop_contract::ClientResult<InventorySnapshot> InventoryClient::tryGetIdentity() const
{
    if (!impl_->iface) {
        return interop_contract::ClientError{
            interop_contract::ClientErrorCode::service_unavailable,
            "inventory client interface proxy is unavailable",
        };
    }

    return invokeQuery<InventorySnapshot>("InventoryClient::tryGetIdentity", [&] {
        auto method = impl_->iface->create_method<
            std::map<std::string, DBus::Variant>()>(std::string(METHOD_GET_IDENTITY));
        return InventoryDbusCodec::decodeSnapshot((*method)());
    });
}

interop_contract::ClientResult<InventoryFields>
InventoryClient::tryGetField(const std::string& fieldName) const
{
    if (!impl_->iface) {
        return interop_contract::ClientError{
            interop_contract::ClientErrorCode::service_unavailable,
            "inventory client interface proxy is unavailable",
        };
    }

    return invokeQuery<InventoryFields>("InventoryClient::tryGetField", [&] {
        auto method = impl_->iface->create_method<
            std::map<std::string, DBus::Variant>(std::string)>(std::string(METHOD_GET_FIELD));
        return InventoryDbusCodec::decodeFields((*method)(fieldName));
    });
}

interop_contract::ClientResult<interop_contract::inventory::SourceStateMap>
InventoryClient::tryGetSourceStates() const
{
    if (!impl_->iface) {
        return interop_contract::ClientError{
            interop_contract::ClientErrorCode::service_unavailable,
            "inventory client interface proxy is unavailable",
        };
    }

    return invokeQuery<interop_contract::inventory::SourceStateMap>(
        "InventoryClient::tryGetSourceStates", [&] {
            auto method = impl_->iface->create_method<
                std::map<std::string, std::map<std::string, DBus::Variant>>()>(
                    std::string(METHOD_GET_SOURCE_STATES));
            return InventoryDbusCodec::decodeSourceStates((*method)());
        });
}

interop_contract::ClientResult<bool> InventoryClient::tryGetReady() const
{
    if (!impl_->iface) {
        return interop_contract::ClientError{
            interop_contract::ClientErrorCode::service_unavailable,
            "inventory client interface proxy is unavailable",
        };
    }

    return invokeQuery<bool>("InventoryClient::tryGetReady", [&] {
        auto method = impl_->iface->create_method<bool()>(std::string(METHOD_GET_READY));
        return (*method)();
    });
}

interop_contract::ClientResult<std::string> InventoryClient::tryGetPhase() const
{
    if (!impl_->iface) {
        return interop_contract::ClientError{
            interop_contract::ClientErrorCode::service_unavailable,
            "inventory client interface proxy is unavailable",
        };
    }

    return invokeQuery<std::string>("InventoryClient::tryGetPhase", [&] {
        auto method = impl_->iface->create_method<std::string()>(std::string(METHOD_GET_PHASE));
        return (*method)();
    });
}

interop_contract::ClientResult<uint64_t> InventoryClient::tryGetVersion() const
{
    if (!impl_->iface) {
        return interop_contract::ClientError{
            interop_contract::ClientErrorCode::service_unavailable,
            "inventory client interface proxy is unavailable",
        };
    }

    return invokeQuery<uint64_t>("InventoryClient::tryGetVersion", [&] {
        auto method = impl_->iface->create_method<uint64_t()>(std::string(METHOD_GET_VERSION));
        return (*method)();
    });
}

interop_contract::ClientResult<interop_contract::inventory::InventoryIssues>
InventoryClient::tryGetIssues() const
{
    if (!impl_->iface) {
        return interop_contract::ClientError{
            interop_contract::ClientErrorCode::service_unavailable,
            "inventory client interface proxy is unavailable",
        };
    }

    return invokeQuery<interop_contract::inventory::InventoryIssues>(
        "InventoryClient::tryGetIssues", [&] {
            auto method = impl_->iface->create_method<
                std::map<std::string, std::map<std::string, DBus::Variant>>()>(
                    std::string(METHOD_GET_ISSUES));
            return InventoryDbusCodec::decodeIssues((*method)());
        });
}

interop_contract::ClientResult<void> InventoryClient::tryRefresh() const
{
    if (!impl_->iface) {
        return interop_contract::ClientError{
            interop_contract::ClientErrorCode::service_unavailable,
            "inventory client interface proxy is unavailable",
        };
    }

    return invokeQuery<void>("InventoryClient::tryRefresh", [&] {
        auto method = impl_->iface->create_method<void()>(std::string(METHOD_REFRESH));
        (*method)();
        return interop_contract::ClientResult<void>{};
    });
}

interop_contract::ClientResult<bool> InventoryClient::tryWaitReady(std::chrono::milliseconds timeout) const
{
    constexpr auto kPollInterval = std::chrono::milliseconds(200);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::optional<interop_contract::ClientError> lastError;

    while (std::chrono::steady_clock::now() < deadline) {
        const auto ready = tryGetReady();
        if (ready && ready.value()) {
            return true;
        }
        if (!ready) {
            const auto& error = ready.error();
            if (error.code != interop_contract::ClientErrorCode::service_unavailable) {
                return ready;
            }
            lastError = error;
        }
        std::this_thread::sleep_for(kPollInterval);
    }

    if (const auto ready = tryGetReady(); ready && ready.value()) {
        return true;
    }

    std::string message = "inventory service did not become ready before timeout";
    if (lastError) {
        message += ": ";
        message += lastError->message;
    }
    return interop_contract::ClientError{
        interop_contract::ClientErrorCode::timeout,
        std::move(message),
    };
}

InventorySnapshot InventoryClient::getIdentity() const
{
    const auto result = tryGetIdentity();
    return result ? result.value() : InventorySnapshot{};
}

InventoryFields InventoryClient::getField(const std::string& fieldName) const
{
    const auto result = tryGetField(fieldName);
    return result ? result.value() : InventoryFields{};
}

interop_contract::inventory::SourceStateMap InventoryClient::getSourceStates() const
{
    const auto result = tryGetSourceStates();
    return result ? result.value() : interop_contract::inventory::SourceStateMap{};
}

bool InventoryClient::getReady() const
{
    const auto result = tryGetReady();
    return result ? result.value() : false;
}

std::string InventoryClient::getPhase() const
{
    const auto result = tryGetPhase();
    return result ? result.value() : "unknown";
}

uint64_t InventoryClient::getVersion() const
{
    const auto result = tryGetVersion();
    return result ? result.value() : 0;
}

interop_contract::inventory::InventoryIssues InventoryClient::getIssues() const
{
    const auto result = tryGetIssues();
    return result ? result.value() : interop_contract::inventory::InventoryIssues{};
}

void InventoryClient::refresh() const
{
    (void)tryRefresh();
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
    const auto result = tryWaitReady(timeout);
    return result ? result.value() : false;
}

} // namespace RSCGroup
