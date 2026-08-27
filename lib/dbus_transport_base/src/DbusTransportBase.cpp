#include "dbus_transport_base/DbusTransportBase.h"
#include "dbus_transport_base/DbusServiceAdapter.h"

#include <api/IInventoryQueryService.h>

#include <dbus-cxx.h>
#include <glog/logging.h>

#include <cstdint>
#include <stdexcept>

namespace RSCGroup {

struct DbusTransportBase::Impl {
    std::atomic<bool> registered{false};
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

DbusTransportBase::~DbusTransportBase()
{
    if (running_.load()) {
        stop();
    }
}

void DbusTransportBase::start(IInventoryQueryService& queryService)
{
    if (running_.load()) return;

    adapter_->setService(&queryService);
    adapter_->onTransportStarting();

    try {
        const auto nameResult = connection_->request_name(serviceName_);
        if (nameResult != DBus::RequestNameResponse::PrimaryOwner &&
            nameResult != DBus::RequestNameResponse::AlreadyOwner) {
            LOG(ERROR) << "DbusTransportBase: failed to acquire bus name '"
                       << serviceName_ << "' (result="
                       << static_cast<std::uint32_t>(nameResult) << ")";
            adapter_->setService(nullptr);
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
        adapter_->setService(nullptr);
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
    adapter_->setService(nullptr);
}

void DbusTransportBase::publishInventoryChanged(const std::string& fieldPath)
{
    if (!running_.load(std::memory_order_acquire)) return;
    adapter_->publishInventoryChanged(fieldPath);
}

void DbusTransportBase::publishSourceStateChanged(const std::string& sourceName)
{
    if (!running_.load(std::memory_order_acquire)) return;
    adapter_->publishSourceStateChanged(sourceName);
}

void DbusTransportBase::publishReadyChanged(bool ready)
{
    if (!running_.load(std::memory_order_acquire)) return;
    adapter_->publishReadyChanged(ready);
}

} // namespace RSCGroup
