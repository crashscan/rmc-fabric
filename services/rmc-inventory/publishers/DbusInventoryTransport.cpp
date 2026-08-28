#include "DbusInventoryTransport.h"
#include "InventoryDbusAdapter.h"

namespace RSCGroup {

DbusInventoryTransport::DbusInventoryTransport(std::shared_ptr<DBus::Connection> connection,
                                               std::string serviceName,
                                               std::string objectPath,
                                               std::string interfaceName)
    : DbusTransportBase(std::move(connection),
                        std::make_unique<InventoryDbusAdapter>(),
                        std::move(serviceName),
                        std::move(objectPath),
                        std::move(interfaceName))
{
}

DbusInventoryTransport::~DbusInventoryTransport() = default;

InventoryDbusAdapter* DbusInventoryTransport::inventoryAdapter() const
{
    return static_cast<InventoryDbusAdapter*>(getAdapter());
}

void DbusInventoryTransport::start(IInventoryQueryService& queryService)
{
    inventoryAdapter()->setService(&queryService);
    DbusTransportBase::start();
}

void DbusInventoryTransport::stop()
{
    DbusTransportBase::stop();
    inventoryAdapter()->setService(nullptr);
}

void DbusInventoryTransport::publishInventoryChanged(const std::string& fieldPath)
{
    if (!isRunning()) return;
    inventoryAdapter()->publishInventoryChanged(fieldPath);
}

void DbusInventoryTransport::publishSourceStateChanged(const std::string& sourceName)
{
    if (!isRunning()) return;
    inventoryAdapter()->publishSourceStateChanged(sourceName);
}

void DbusInventoryTransport::publishReadyChanged(bool ready)
{
    if (!isRunning()) return;
    inventoryAdapter()->publishReadyChanged(ready);
}

} // namespace RSCGroup
