#include "DbusInventoryTransport.h"
#include "inventory_transport/inventory/InventoryDbusAdapter.h"

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

} // namespace RSCGroup
