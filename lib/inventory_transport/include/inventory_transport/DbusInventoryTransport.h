#pragma once

#include "dbus/DbusTransportBase.h"

#include "inventory_transport/inventory/InventoryDbusAdapter.h"

#include <memory>
#include <string>

namespace DBus {
class Connection;
} // namespace DBus

namespace RSCGroup {

class IInventoryQueryService;

class DbusInventoryTransport final : public DbusTransportBase {
public:
    DbusInventoryTransport(std::shared_ptr<DBus::Connection> connection,
                           std::string serviceName,
                           std::string objectPath,
                           std::string interfaceName);
    ~DbusInventoryTransport() override;

    DbusInventoryTransport(const DbusInventoryTransport&) = delete;
    DbusInventoryTransport& operator=(const DbusInventoryTransport&) = delete;

};

} // namespace RSCGroup