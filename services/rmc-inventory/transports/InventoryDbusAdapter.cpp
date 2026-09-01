#include "InventoryDbusAdapter.h"

#include "InventoryDbusCodec.h"
#include <IInventoryQueryService.h>

#include <inventory.hpp>

#include <dbus-cxx.h>
#include <glog/logging.h>
#include <sigc++/sigc++.h>

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>

namespace RSCGroup {
namespace {

using namespace interop_contract::inventory;

struct InventoryQueryHandler {
    /// Thread-safe service access: shared lock held during the call, exclusive
    /// lock taken by onTransportStopping() to clear the binding.
    ServiceBinding<IInventoryQueryService>* binding = nullptr;

    std::map<std::string, DBus::Variant> GetIdentity()
    {
        if (auto guard = binding->acquire()) {
            try { return InventoryDbusCodec::encodeSnapshot(guard->getIdentity()); }
            catch (const std::exception& e) { LOG(ERROR) << "GetIdentity failed: " << e.what(); }
        }
        return {};
    }

    std::map<std::string, DBus::Variant> GetField(std::string fieldName)
    {
        if (auto guard = binding->acquire()) {
            try { return InventoryDbusCodec::encodeFields(guard->getField(fieldName)); }
            catch (const std::exception& e) { LOG(ERROR) << "GetField failed: " << e.what(); }
        }
        return {};
    }

    std::map<std::string, std::map<std::string, DBus::Variant>> GetSourceStates()
    {
        if (auto guard = binding->acquire()) {
            try { return InventoryDbusCodec::encodeSourceStates(guard->getSourceStates()); }
            catch (const std::exception& e) { LOG(ERROR) << "GetSourceStates failed: " << e.what(); }
        }
        return {};
    }

    std::map<std::string, std::map<std::string, DBus::Variant>> GetIssues()
    {
        if (auto guard = binding->acquire()) {
            try { return InventoryDbusCodec::encodeIssues(guard->getIssues()); }
            catch (const std::exception& e) { LOG(ERROR) << "GetIssues failed: " << e.what(); }
        }
        return {};
    }

    bool GetReady()
    {
        if (auto guard = binding->acquire()) return guard->getReady();
        return false;
    }

    std::string GetPhase()
    {
        if (auto guard = binding->acquire()) return guard->getPhase();
        return "unknown";
    }

    uint64_t GetVersion()
    {
        if (auto guard = binding->acquire()) return guard->getVersion();
        return 0;
    }

    void Refresh()
    {
        if (auto guard = binding->acquire()) guard->refresh();
    }
};

} // namespace

InventoryDbusAdapter::InventoryDbusAdapter()
    : handler_(std::make_shared<InventoryQueryHandler>())
{
    handler_->binding = &binding_;
}

void InventoryDbusAdapter::quiesceQueries() noexcept
{
    binding_.detach();
}

void InventoryDbusAdapter::onTransportStopping()
{
    quiesceQueries();
}

void InventoryDbusAdapter::setService(IInventoryQueryService* service)
{
    binding_.bind(service);
}

void InventoryDbusAdapter::bind(const std::shared_ptr<DBus::Object>& object,
                                const std::string& interfaceName)
{
    if (!object) {
        throw std::invalid_argument("InventoryDbusAdapter::bind: object is null");
    }

    createSignals(object, interfaceName);
    bindMethods(object, interfaceName);
}

void InventoryDbusAdapter::createSignals(const std::shared_ptr<DBus::Object>& object,
                                         const std::string& interfaceName)
{
    using namespace interop_contract::inventory;

    signalInventoryChanged_ = createStringSignal(object,
        interfaceName, std::string(SIGNAL_INVENTORY_CHANGED));
    signalSourceStateChanged_ = createStringSignal(object,
        interfaceName, std::string(SIGNAL_SOURCE_STATE_CHANGED));
    signalReadyChanged_ = createBoolSignal(object,
        interfaceName, std::string(SIGNAL_READY_CHANGED));
}

void InventoryDbusAdapter::bindMethods(const std::shared_ptr<DBus::Object>& object,
                                       const std::string& interfaceName)
{
    using namespace interop_contract::inventory;

    object->create_method<std::map<std::string, DBus::Variant>()>(
        interfaceName, std::string(METHOD_GET_IDENTITY),
        sigc::mem_fun(*handler_, &InventoryQueryHandler::GetIdentity));
    object->create_method<std::map<std::string, DBus::Variant>(std::string)>(
        interfaceName, std::string(METHOD_GET_FIELD),
        sigc::mem_fun(*handler_, &InventoryQueryHandler::GetField));
    object->create_method<std::map<std::string, std::map<std::string, DBus::Variant>>()>(
        interfaceName, std::string(METHOD_GET_SOURCE_STATES),
        sigc::mem_fun(*handler_, &InventoryQueryHandler::GetSourceStates));
    object->create_method<bool()>(
        interfaceName, std::string(METHOD_GET_READY),
        sigc::mem_fun(*handler_, &InventoryQueryHandler::GetReady));
    object->create_method<std::string()>(
        interfaceName, std::string(METHOD_GET_PHASE),
        sigc::mem_fun(*handler_, &InventoryQueryHandler::GetPhase));
    object->create_method<uint64_t()>(
        interfaceName, std::string(METHOD_GET_VERSION),
        sigc::mem_fun(*handler_, &InventoryQueryHandler::GetVersion));
    object->create_method<void()>(
        interfaceName, std::string(METHOD_REFRESH),
        sigc::mem_fun(*handler_, &InventoryQueryHandler::Refresh));
    object->create_method<std::map<std::string, std::map<std::string, DBus::Variant>>()>(
        interfaceName, std::string(METHOD_GET_ISSUES),
        sigc::mem_fun(*handler_, &InventoryQueryHandler::GetIssues));
}

void InventoryDbusAdapter::publishInventoryChanged(const std::string& fieldPath)
{
    if (!signalInventoryChanged_) return;
    try {
        signalInventoryChanged_->emit(fieldPath);
    } catch (const std::exception& e) {
        LOG(ERROR) << "publishInventoryChanged failed: " << e.what();
    }
}

void InventoryDbusAdapter::publishSourceStateChanged(const std::string& sourceName)
{
    if (!signalSourceStateChanged_) return;
    try {
        signalSourceStateChanged_->emit(sourceName);
    } catch (const std::exception& e) {
        LOG(ERROR) << "publishSourceStateChanged failed: " << e.what();
    }
}

void InventoryDbusAdapter::publishReadyChanged(bool ready)
{
    if (!signalReadyChanged_) return;
    try {
        signalReadyChanged_->emit(ready);
    } catch (const std::exception& e) {
        LOG(ERROR) << "publishReadyChanged failed: " << e.what();
    }
}

} // namespace RSCGroup
