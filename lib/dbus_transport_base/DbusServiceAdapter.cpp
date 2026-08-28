#include "DbusServiceAdapter.h"

#include <dbus-cxx.h>
#include <glog/logging.h>

#include <stdexcept>

namespace RSCGroup {

std::shared_ptr<DBus::Signal<void(std::string)>> DbusServiceAdapter::createStringSignal(
    const std::shared_ptr<DBus::Object>& object,
    const std::string& interfaceName,
    const std::string& signalName)
{
    if (!object) {
        throw std::invalid_argument("DbusServiceAdapter::createStringSignal: object is null");
    }
    try {
        return object->create_signal<void(std::string)>(interfaceName, signalName);
    } catch (const std::exception& e) {
        LOG(ERROR) << "Failed to create string signal '" << signalName << "': " << e.what();
        throw;
    }
}

std::shared_ptr<DBus::Signal<void(bool)>> DbusServiceAdapter::createBoolSignal(
    const std::shared_ptr<DBus::Object>& object,
    const std::string& interfaceName,
    const std::string& signalName)
{
    if (!object) {
        throw std::invalid_argument("DbusServiceAdapter::createBoolSignal: object is null");
    }
    try {
        return object->create_signal<void(bool)>(interfaceName, signalName);
    } catch (const std::exception& e) {
        LOG(ERROR) << "Failed to create bool signal '" << signalName << "': " << e.what();
        throw;
    }
}

} // namespace RSCGroup
