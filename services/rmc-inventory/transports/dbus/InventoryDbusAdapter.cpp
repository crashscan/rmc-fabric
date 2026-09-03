#include "InventoryDbusAdapter.h"

#include "InventoryDbusCodec.h"
#include "InventoryQueryHandler.h"
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

InventoryDbusAdapter::InventoryDbusAdapter()
    : handler_(std::make_shared<InventoryQueryHandler>(binding_)) {
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
        sigc::mem_fun(*handler_, &InventoryQueryHandler::getIdentity));
    object->create_method<std::map<std::string, DBus::Variant>(std::string)>(
        interfaceName, std::string(METHOD_GET_FIELD),
        sigc::mem_fun(*handler_, &InventoryQueryHandler::getField));
    object->create_method<std::map<std::string, std::map<std::string, DBus::Variant>>()>(
        interfaceName, std::string(METHOD_GET_SOURCE_STATES),
        sigc::mem_fun(*handler_, &InventoryQueryHandler::getSourceStates));
    object->create_method<bool()>(
        interfaceName, std::string(METHOD_GET_READY),
        sigc::mem_fun(*handler_, &InventoryQueryHandler::getReady));
    object->create_method<std::string()>(
        interfaceName, std::string(METHOD_GET_PHASE),
        sigc::mem_fun(*handler_, &InventoryQueryHandler::getPhase));
    object->create_method<uint64_t()>(
        interfaceName, std::string(METHOD_GET_VERSION),
        sigc::mem_fun(*handler_, &InventoryQueryHandler::getVersion));
    object->create_method<void()>(
        interfaceName, std::string(METHOD_REFRESH),
        sigc::mem_fun(*handler_, &InventoryQueryHandler::refresh));
    object->create_method<std::map<std::string, std::map<std::string, DBus::Variant>>()>(
        interfaceName, std::string(METHOD_GET_ISSUES),
        sigc::mem_fun(*handler_, &InventoryQueryHandler::getIssues));
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
