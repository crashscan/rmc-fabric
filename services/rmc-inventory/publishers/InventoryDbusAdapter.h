#pragma once

#include <DbusServiceAdapter.h>

#include <memory>
#include <string>
#include <map>

namespace DBus {
class Object;
class Signal;
}

namespace RSCGroup {

class IInventoryQueryService;
struct InventoryQueryHandler;

class InventoryDbusAdapter : public DbusServiceAdapter {
public:
    InventoryDbusAdapter() = default;

    InventoryDbusAdapter(const InventoryDbusAdapter&) = delete;
    InventoryDbusAdapter& operator=(const InventoryDbusAdapter&) = delete;

    void setService(IInventoryQueryService* service);
    [[nodiscard]] IInventoryQueryService* getService() const;

    void bind(const std::shared_ptr<DBus::Object>& object,
              const std::string& interfaceName) override;

    void onTransportStopping() override;

    void publishInventoryChanged(const std::string& fieldPath);
    void publishSourceStateChanged(const std::string& sourceName);
    void publishReadyChanged(bool ready);

private:
    IInventoryQueryService* service_ = nullptr;
    std::shared_ptr<InventoryQueryHandler> handler_;

    std::shared_ptr<DBus::Signal<void(std::string)>> signalInventoryChanged_;
    std::shared_ptr<DBus::Signal<void(std::string)>> signalSourceStateChanged_;
    std::shared_ptr<DBus::Signal<void(bool)>> signalReadyChanged_;

    void createSignals(const std::shared_ptr<DBus::Object>& object,
                       const std::string& interfaceName);
    void bindMethods(const std::shared_ptr<DBus::Object>& object,
                     const std::string& interfaceName);
};

} // namespace RSCGroup
