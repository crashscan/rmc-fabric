#pragma once

#include <memory>
#include <string>
#include <map>

namespace DBus {
class Object;
template <typename> class Signal;
}

namespace RSCGroup {

class IInventoryQueryService;

class DbusServiceAdapter {
public:
    DbusServiceAdapter() = default;
    virtual ~DbusServiceAdapter() = default;

    DbusServiceAdapter(const DbusServiceAdapter&) = delete;
    DbusServiceAdapter& operator=(const DbusServiceAdapter&) = delete;

    virtual void setService(IInventoryQueryService* service) = 0;
    [[nodiscard]] virtual IInventoryQueryService* getService() const = 0;

    virtual void bind(const std::shared_ptr<DBus::Object>& object,
                      const std::string& interfaceName) = 0;

    virtual void publishInventoryChanged(const std::string& fieldPath) = 0;
    virtual void publishSourceStateChanged(const std::string& sourceName) = 0;
    virtual void publishReadyChanged(bool ready) = 0;

    // Adapter lifecycle: called by transport
    virtual void onTransportStarting() {}
    virtual void onTransportStopping() {}

protected:
    [[nodiscard]] std::shared_ptr<DBus::Signal<void(std::string)>> createStringSignal(
        const std::shared_ptr<DBus::Object>& object,
        const std::string& interfaceName,
        const std::string& signalName);

    [[nodiscard]] std::shared_ptr<DBus::Signal<void(bool)>> createBoolSignal(
        const std::shared_ptr<DBus::Object>& object,
        const std::string& interfaceName,
        const std::string& signalName);
};

} // namespace RSCGroup