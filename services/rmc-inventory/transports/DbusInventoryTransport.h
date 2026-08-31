#pragma once

#include <DbusTransportBase.h>
#include <IInventoryTransport.h>

#include "InventoryDbusAdapter.h"

#include <memory>
#include <string>

namespace DBus {
class Connection;
} // namespace DBus

namespace RSCGroup {

class IInventoryQueryService;

class DbusInventoryTransport final : public IInventoryTransport, public DbusTransportBase {
public:
    DbusInventoryTransport(std::shared_ptr<DBus::Connection> connection,
                           std::string serviceName,
                           std::string objectPath,
                           std::string interfaceName);
    ~DbusInventoryTransport() override;

    DbusInventoryTransport(const DbusInventoryTransport&) = delete;
    DbusInventoryTransport& operator=(const DbusInventoryTransport&) = delete;

    void bindQueryService(IInventoryQueryService& queryService) override;

    [[nodiscard]] bool start() override;
    void stop() override;
    void quiesceQueries() override;
    [[nodiscard]] std::string name() const override;

    void publishInventoryChanged(const std::string& fieldPath) override;
    void publishSourceStateChanged(const std::string& sourceName) override;
    void publishReadyChanged(bool ready) override;

private:
    [[nodiscard]] InventoryDbusAdapter* inventoryAdapter() const;
};

} // namespace RSCGroup
