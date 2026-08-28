#include "DbusTransportBase.h"
#include "DbusServiceAdapter.h"

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

    // If no external connection was provided, create one from busType_.
    if (!connection_) {
        impl_->dispatcher = DBus::StandaloneDispatcher::create();
        const auto busT = (busType_ == "session") ? DBus::BusType::SESSION
                                                   : DBus::BusType::SYSTEM;
        connection_ = impl_->dispatcher->create_connection(busT);
    }

    adapter_->onTransportStarting();

    try {
        const auto nameResult = connection_->request_name(serviceName_);
        if (nameResult != DBus::RequestNameResponse::PrimaryOwner &&
            nameResult != DBus::RequestNameResponse::AlreadyOwner) {
            LOG(ERROR) << "DbusTransportBase: failed to acquire bus name '"
                       << serviceName_ << "' (result="
                       << static_cast<std::uint32_t>(nameResult) << ")";
            throw std::runtime_error("DbusTransportBase: bus name unavailable");
        }

        object_ = connection_->create_object(objectPath_);
        connection_->register_object(object_);
        impl_->registered.store(true);

        adapter_->bind(object_, interfaceName_);
        onAdapterBound();

        running_.store(true, std::memory_order_release);
        LOG(INFO) << "DbusTransportBase started: " << serviceName_;
    } catch (const std::exception& e) {
        running_.store(false, std::memory_order_release);
        if (impl_->registered.load()) {
            connection_->unregister_object(objectPath_);
            impl_->registered.store(false);
        }
        object_.reset();
        if (impl_->dispatcher) {
            connection_.reset();
            impl_->dispatcher.reset();
        }
        LOG(ERROR) << "DbusTransportBase start failed: " << e.what();
        throw;
    }
}

void DbusTransportBase::stop()
{
    if (!running_.exchange(false, std::memory_order_acq_rel)) return;

    adapter_->onTransportStopping();

    if (object_ && connection_ && impl_->registered.load()) {
        connection_->unregister_object(objectPath_);
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
