#include "DbusClient.h"
#include "NetworkObservationDbusCodec.h"

#include <DecodeError.hpp>
#include <IngressLimits.hpp>
#include <network_observation/NetworkObservationContracts.hpp>

#include <dbus-cxx.h>
#include <glog/logging.h>
#include <sigc++/sigc++.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace RSCGroup {

namespace {
namespace contract = interop_contract::network_observation;

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

void validateStringList(const std::vector<std::string>& values, const char* fieldName)
{
    if (values.size() > interop_contract::ingress::network_observation::kMaxCandidates) {
        throw interop_contract::DecodeError(
            interop_contract::DecodeErrorCode::limit_exceeded,
            std::string(fieldName) + " exceeds ingress list limit");
    }
    for (const auto& value : values) {
        if (value.size() > interop_contract::ingress::kMaxStringLength) {
            throw interop_contract::DecodeError(
                interop_contract::DecodeErrorCode::limit_exceeded,
                std::string(fieldName) + " contains an oversized string");
        }
    }
}

} // anonymous namespace

struct DbusClient::Impl
{
    std::string busType;
    std::shared_ptr<DBus::StandaloneDispatcher> dispatcher;
    std::shared_ptr<DBus::Connection> connection;
    std::shared_ptr<DBus::ObjectProxy> proxy;
    std::shared_ptr<DBus::InterfaceProxy> iface;
    bool connected = false;

    std::shared_ptr<DBus::SignalProxy<void()>> sigLocalStateChanged;
    std::shared_ptr<DBus::SignalProxy<void(std::string)>> sigInterfaceChanged;
    std::shared_ptr<DBus::SignalProxy<void(std::string)>> sigCandidateChanged;
    std::shared_ptr<DBus::SignalProxy<void(std::string)>> sigInterfaceRemoved;
    std::shared_ptr<DBus::SignalProxy<void(std::string)>> sigCandidateRemoved;
    std::shared_ptr<DBus::SignalProxy<void(bool)>> sigReadyChanged;
    void reset()
    {
        sigLocalStateChanged.reset();
        sigInterfaceChanged.reset();
        sigCandidateChanged.reset();
        sigInterfaceRemoved.reset();
        sigCandidateRemoved.reset();
        sigReadyChanged.reset();
        iface.reset();
        proxy.reset();
        connection.reset();
        dispatcher.reset();
        connected = false;
    }
};

DbusClient::DbusClient(const std::string& busType)
    : impl_(std::make_unique<Impl>())
{
    impl_->busType = busType;
}

DbusClient::~DbusClient() = default;

interop_contract::ClientResult<void> DbusClient::tryConnect()
{
    return invokeQuery<void>("DbusClient::tryConnect", [&] {
        impl_->reset();
        impl_->dispatcher = DBus::StandaloneDispatcher::create();
        impl_->connection = impl_->dispatcher->create_connection(toBusType(impl_->busType));
        impl_->proxy = DBus::ObjectProxy::create(
            impl_->connection, std::string(contract::SERVICE_NAME), std::string(contract::OBJECT_PATH));
        impl_->iface = impl_->proxy->create_interface(std::string(contract::INTERFACE));
        impl_->connected = static_cast<bool>(impl_->iface);
        if (!impl_->connected) {
            return interop_contract::ClientResult<void>{
                interop_contract::ClientError{
                    interop_contract::ClientErrorCode::service_unavailable,
                    "network observation interface proxy is unavailable",
                }};
        }
        LOG(INFO) << "DbusClient connected to bus: " << impl_->busType;
        return interop_contract::ClientResult<void>{};
    });
}

bool DbusClient::connect()
{
    return static_cast<bool>(tryConnect());
}

interop_contract::ClientResult<contract::LocalNetworkSnapshot> DbusClient::tryGetLocalSnapshot()
{
    if (!impl_->iface) {
        return interop_contract::ClientError{
            interop_contract::ClientErrorCode::service_unavailable,
            "network observation interface proxy is unavailable",
        };
    }

    return invokeQuery<contract::LocalNetworkSnapshot>("DbusClient::tryGetLocalSnapshot", [&] {
        auto method = impl_->iface->create_method<
            std::map<std::string, DBus::Variant>()>(std::string(contract::METHOD_GET_LOCAL_SNAPSHOT));
        return NetworkObservationDbusCodec::fromVariantMapLocalSnapshot((*method)());
    });
}

interop_contract::ClientResult<std::optional<contract::LocalInterfaceState>>
DbusClient::tryGetInterface(const std::string& ifname)
{
    if (!impl_->iface) {
        return interop_contract::ClientError{
            interop_contract::ClientErrorCode::service_unavailable,
            "network observation interface proxy is unavailable",
        };
    }

    return invokeQuery<std::optional<contract::LocalInterfaceState>>(
        "DbusClient::tryGetInterface", [&] {
            auto method = impl_->iface->create_method<
                std::map<std::string, DBus::Variant>(std::string)>(std::string(contract::METHOD_GET_INTERFACE));
            auto raw = (*method)(ifname);
            if (raw.empty()) {
                return std::optional<contract::LocalInterfaceState>{};
            }
            return std::optional<contract::LocalInterfaceState>{
                NetworkObservationDbusCodec::fromVariantMapIface(raw)};
        });
}

interop_contract::ClientResult<std::vector<std::string>> DbusClient::tryGetRemoteCandidateMacs()
{
    if (!impl_->iface) {
        return interop_contract::ClientError{
            interop_contract::ClientErrorCode::service_unavailable,
            "network observation interface proxy is unavailable",
        };
    }

    return invokeQuery<std::vector<std::string>>("DbusClient::tryGetRemoteCandidateMacs", [&] {
        auto method = impl_->iface->create_method<
            std::vector<std::string>()>(std::string(contract::METHOD_GET_REMOTE_CANDIDATE_MACS));
        auto raw = (*method)();
        validateStringList(raw, "remote candidate MAC list");
        return raw;
    });
}

interop_contract::ClientResult<std::optional<contract::RemoteCandidate>>
DbusClient::tryGetCandidateByMac(const std::string& mac)
{
    if (!impl_->iface) {
        return interop_contract::ClientError{
            interop_contract::ClientErrorCode::service_unavailable,
            "network observation interface proxy is unavailable",
        };
    }

    return invokeQuery<std::optional<contract::RemoteCandidate>>(
        "DbusClient::tryGetCandidateByMac", [&] {
            auto method = impl_->iface->create_method<
                std::map<std::string, DBus::Variant>(std::string)>(
                    std::string(contract::METHOD_GET_CANDIDATE_BY_MAC));
            auto raw = (*method)(mac);
            if (raw.empty()) {
                return std::optional<contract::RemoteCandidate>{};
            }
            return std::optional<contract::RemoteCandidate>{
                NetworkObservationDbusCodec::fromVariantMapCandidate(raw)};
        });
}

interop_contract::ClientResult<contract::ObservationIssues> DbusClient::tryGetIssues()
{
    if (!impl_->iface) {
        return interop_contract::ClientError{
            interop_contract::ClientErrorCode::service_unavailable,
            "network observation interface proxy is unavailable",
        };
    }

    return invokeQuery<contract::ObservationIssues>("DbusClient::tryGetIssues", [&] {
        auto method = impl_->iface->create_method<
            std::map<std::string, std::map<std::string, DBus::Variant>>()>(
                std::string(contract::METHOD_GET_ISSUES));
        return NetworkObservationDbusCodec::decodeIssues((*method)());
    });
}

interop_contract::ClientResult<bool> DbusClient::tryGetReady()
{
    if (!impl_->iface) {
        return interop_contract::ClientError{
            interop_contract::ClientErrorCode::service_unavailable,
            "network observation interface proxy is unavailable",
        };
    }

    return invokeQuery<bool>("DbusClient::tryGetReady", [&] {
        auto method = impl_->iface->create_method<bool()>(std::string(contract::METHOD_GET_READY));
        return (*method)();
    });
}

interop_contract::ClientResult<std::string> DbusClient::tryGetPhase()
{
    if (!impl_->iface) {
        return interop_contract::ClientError{
            interop_contract::ClientErrorCode::service_unavailable,
            "network observation interface proxy is unavailable",
        };
    }

    return invokeQuery<std::string>("DbusClient::tryGetPhase", [&] {
        auto method = impl_->iface->create_method<std::string()>(std::string(contract::METHOD_GET_PHASE));
        return (*method)();
    });
}

contract::LocalNetworkSnapshot DbusClient::getLocalSnapshot()
{
    const auto result = tryGetLocalSnapshot();
    return result ? result.value() : contract::LocalNetworkSnapshot{};
}

std::optional<contract::LocalInterfaceState> DbusClient::getInterface(const std::string& ifname)
{
    const auto result = tryGetInterface(ifname);
    return result ? result.value() : std::nullopt;
}

std::vector<std::string> DbusClient::getRemoteCandidateMacs()
{
    const auto result = tryGetRemoteCandidateMacs();
    return result ? result.value() : std::vector<std::string>{};
}

std::optional<contract::RemoteCandidate> DbusClient::getCandidateByMac(const std::string& mac)
{
    const auto result = tryGetCandidateByMac(mac);
    return result ? result.value() : std::nullopt;
}

contract::ObservationIssues DbusClient::getIssues()
{
    const auto result = tryGetIssues();
    return result ? result.value() : contract::ObservationIssues{};
}

bool DbusClient::getReady()
{
    const auto result = tryGetReady();
    return result ? result.value() : false;
}

std::string DbusClient::getPhase()
{
    const auto result = tryGetPhase();
    return result ? result.value() : "unknown";
}

void DbusClient::onLocalStateChanged(VoidCallback cb)
{
    if (!impl_->iface) return;
    if (!impl_->sigLocalStateChanged)
        impl_->sigLocalStateChanged = impl_->iface->create_signal<void()>(
            std::string(contract::SIGNAL_LOCAL_STATE_CHANGED));
    impl_->sigLocalStateChanged->connect(sigc::slot<void()>(cb));
}

void DbusClient::onInterfaceChanged(StringCallback cb)
{
    if (!impl_->iface) return;
    if (!impl_->sigInterfaceChanged)
        impl_->sigInterfaceChanged = impl_->iface->create_signal<void(std::string)>(
            std::string(contract::SIGNAL_INTERFACE_CHANGED));
    impl_->sigInterfaceChanged->connect(sigc::slot<void(std::string)>(cb));
}

void DbusClient::onCandidateChanged(StringCallback cb)
{
    if (!impl_->iface) return;
    if (!impl_->sigCandidateChanged)
        impl_->sigCandidateChanged = impl_->iface->create_signal<void(std::string)>(
            std::string(contract::SIGNAL_CANDIDATE_CHANGED));
    impl_->sigCandidateChanged->connect(sigc::slot<void(std::string)>(cb));
}

void DbusClient::onInterfaceRemoved(StringCallback cb)
{
    if (!impl_->iface) return;
    if (!impl_->sigInterfaceRemoved)
        impl_->sigInterfaceRemoved = impl_->iface->create_signal<void(std::string)>(
            std::string(contract::SIGNAL_INTERFACE_REMOVED));
    impl_->sigInterfaceRemoved->connect(sigc::slot<void(std::string)>(cb));
}

void DbusClient::onCandidateRemoved(StringCallback cb)
{
    if (!impl_->iface) return;
    if (!impl_->sigCandidateRemoved)
        impl_->sigCandidateRemoved = impl_->iface->create_signal<void(std::string)>(
            std::string(contract::SIGNAL_CANDIDATE_REMOVED));
    impl_->sigCandidateRemoved->connect(sigc::slot<void(std::string)>(cb));
}

void DbusClient::onReadyChanged(BoolCallback cb)
{
    if (!impl_->iface) return;
    if (!impl_->sigReadyChanged)
        impl_->sigReadyChanged = impl_->iface->create_signal<void(bool)>(
            std::string(contract::SIGNAL_READY_CHANGED));
    impl_->sigReadyChanged->connect(sigc::slot<void(bool)>(cb));
}

} // namespace RSCGroup
