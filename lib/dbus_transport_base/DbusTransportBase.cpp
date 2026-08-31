#include "DbusTransportBase.h"
#include "DbusServiceAdapter.h"
#include <OperationalDiagnostics.h>

#include <dbus-cxx.h>
#include <glog/logging.h>

#include <cstdint>
#include <stdexcept>

namespace RSCGroup {

struct DbusTransportBase::Impl {
    std::atomic<bool> registered{false};
    // Owned dispatcher/connection used when the busType constructor is invoked.
    std::shared_ptr<DBus::StandaloneDispatcher> dispatcher;
};

DbusTransportBase::DbusTransportBase(std::shared_ptr<DBus::Connection> connection,
                                     std::unique_ptr<DbusServiceAdapter> adapter,
                                     std::string serviceName,
                                     std::string objectPath,
                                     std::string interfaceName)
    : impl_(std::make_unique<Impl>())
    , connection_(std::move(connection))
    , adapter_(std::move(adapter))
    , serviceName_(std::move(serviceName))
    , objectPath_(std::move(objectPath))
    , interfaceName_(std::move(interfaceName))
{
    if (!connection_) {
        throw std::invalid_argument("DbusTransportBase: connection is null");
    }
    if (!adapter_) {
        throw std::invalid_argument("DbusTransportBase: adapter is null");
    }
}

DbusTransportBase::DbusTransportBase(std::string busType,
                                     std::unique_ptr<DbusServiceAdapter> adapter,
                                     std::string serviceName,
                                     std::string objectPath,
                                     std::string interfaceName)
    : impl_(std::make_unique<Impl>())
    , adapter_(std::move(adapter))
    , busType_(std::move(busType))
    , serviceName_(std::move(serviceName))
    , objectPath_(std::move(objectPath))
    , interfaceName_(std::move(interfaceName))
{
    if (!adapter_) {
        throw std::invalid_argument("DbusTransportBase: adapter is null");
    }
    if (busType_.empty()) {
        throw std::invalid_argument("DbusTransportBase: busType is empty");
    }
}

DbusTransportBase::~DbusTransportBase()
{
    if (running_.load()) {
        stop();
    }
}

void DbusTransportBase::start()
{
    if (running_.load()) return;

    try {
        // If no external connection was provided, create one from busType_.
        // For externally-managed connections (connection_-constructor), connection_ is
        // always set at construction time and this branch is never taken.
        if (!connection_) {
            impl_->dispatcher = DBus::StandaloneDispatcher::create();
            const auto busT = (busType_ == "session") ? DBus::BusType::SESSION
                                                       : DBus::BusType::SYSTEM;
            connection_ = impl_->dispatcher->create_connection(busT);
        }

        adapter_->onTransportStarting();

        const auto nameResult = connection_->request_name(serviceName_);
        if (nameResult != DBus::RequestNameResponse::PrimaryOwner &&
            nameResult != DBus::RequestNameResponse::AlreadyOwner) {
            diagnostics::logError(serviceName_,
                                  "transport.dbus",
                                  "request_name",
                                  "bus_name_unavailable",
                                  serviceName_,
                                  "request_name result=" + std::to_string(static_cast<std::uint32_t>(nameResult)));
            throw std::runtime_error("DbusTransportBase: bus name unavailable");
        }

        object_ = connection_->create_object(objectPath_);
        connection_->register_object(object_);
        impl_->registered.store(true);

        adapter_->bind(object_, interfaceName_);
        onAdapterBound();

        running_.store(true, std::memory_order_release);
        diagnostics::logInfo(serviceName_, "transport.dbus", "start", "transport_started", serviceName_, "started");
    } catch (const std::exception& e) {
        running_.store(false, std::memory_order_release);
        try {
            adapter_->onTransportStopping();
        } catch (const std::exception& stopError) {
            diagnostics::logError(serviceName_, "transport.dbus", "stop_adapter", "transport_stop_failed", serviceName_, stopError.what());
        } catch (...) {
            diagnostics::logError(serviceName_, "transport.dbus", "stop_adapter", "transport_stop_failed", serviceName_, "unknown exception");
        }
        if (impl_->registered.load() && connection_) {
            try {
                connection_->unregister_object(objectPath_);
            } catch (const std::exception& unregisterError) {
                diagnostics::logError(serviceName_, "transport.dbus", "unregister_object", "transport_stop_failed", objectPath_, unregisterError.what());
            } catch (...) {
                diagnostics::logError(serviceName_, "transport.dbus", "unregister_object", "transport_stop_failed", objectPath_, "unknown exception");
            }
            impl_->registered.store(false);
        }
        object_.reset();
        if (impl_->dispatcher) {
            connection_.reset();
            impl_->dispatcher.reset();
        }
        diagnostics::logError(serviceName_, "transport.dbus", "start", "transport_start_failed", serviceName_, e.what());
        throw;
    }
}

void DbusTransportBase::stop()
{
    if (!running_.exchange(false, std::memory_order_acq_rel)) return;

    try {
        adapter_->onTransportStopping();
    } catch (const std::exception& e) {
        diagnostics::logError(serviceName_, "transport.dbus", "stop_adapter", "transport_stop_failed", serviceName_, e.what());
    } catch (...) {
        diagnostics::logError(serviceName_, "transport.dbus", "stop_adapter", "transport_stop_failed", serviceName_, "unknown exception");
    }

    if (object_ && connection_ && impl_->registered.load()) {
        try {
            connection_->unregister_object(objectPath_);
        } catch (const std::exception& e) {
            diagnostics::logError(serviceName_, "transport.dbus", "unregister_object", "transport_stop_failed", objectPath_, e.what());
        } catch (...) {
            diagnostics::logError(serviceName_, "transport.dbus", "unregister_object", "transport_stop_failed", objectPath_, "unknown exception");
        }
        impl_->registered.store(false);
    }
    object_.reset();

    // Release internally-owned dispatcher and connection if applicable.
    if (impl_->dispatcher) {
        connection_.reset();
        impl_->dispatcher.reset();
    }
}

} // namespace RSCGroup
