#pragma once

#include <ITransport.h>
#include <DbusTransportBase.h>

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

    // ITypedTransport<IInventoryQueryService> (inherited via IInventoryTransport)
    void bindQueryService(IInventoryQueryService& queryService) override;

    // IInventoryTransport
    void start() override;
    void stop() override;

    void publishInventoryChanged(const std::string& fieldPath) override;
    void publishSourceStateChanged(const std::string& sourceName) override;
    void publishReadyChanged(bool ready) override;

private:
    [[nodiscard]] InventoryDbusAdapter* inventoryAdapter() const;
};

} // namespace RSCGroup
