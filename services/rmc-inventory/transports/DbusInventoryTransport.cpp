#include "DbusInventoryTransport.h"

#include "InventoryDbusAdapter.h"

#include <glog/logging.h>

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
    return getTypedAdapter<InventoryDbusAdapter>();
}

void DbusInventoryTransport::bindQueryService(IInventoryQueryService& queryService)
{
    inventoryAdapter()->setService(&queryService);
}

bool DbusInventoryTransport::start()
{
    try {
        DbusTransportBase::start();
        return true;
    } catch (const std::exception& e) {
        LOG(ERROR) << "DbusInventoryTransport start failed: " << e.what();
        inventoryAdapter()->setService(nullptr);
        return false;
    }
}

void DbusInventoryTransport::stop()
{
    DbusTransportBase::stop();
}

void DbusInventoryTransport::quiesceQueries() noexcept
{
    DbusTransportBase::quiesceQueries();
}

std::string DbusInventoryTransport::name() const
{
    return "dbus";
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
