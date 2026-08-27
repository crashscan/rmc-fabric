#include "inventory_transport/TransportFactory.h"
#include "inventory_transport/DbusInventoryTransport.h"
#include "inventory_transport/StdoutInventoryTransport.h"

#include <interop_contract/inventory.hpp>
#include <glog/logging.h>

namespace RSCGroup {

std::shared_ptr<ITransport> TransportFactory::create(
    const std::string& name,
    std::shared_ptr<DBus::Connection> connection,
    std::string serviceName,
    std::string objectPath,
    std::string interfaceName)
{
    if (name == "stdout") {
        return std::make_shared<StdoutInventoryTransport>();
    }

    if (name == "dbus") {
        if (!connection) {
            LOG(ERROR) << "TransportFactory: 'dbus' transport requires a connection";
            return nullptr;
        }
        if (serviceName.empty())   serviceName   = std::string(interop_contract::inventory::SERVICE_NAME);
        if (objectPath.empty())    objectPath    = std::string(interop_contract::inventory::OBJECT_PATH);
        if (interfaceName.empty()) interfaceName = std::string(interop_contract::inventory::INTERFACE);
        return std::make_shared<DbusInventoryTransport>(
            std::move(connection), std::move(serviceName),
            std::move(objectPath), std::move(interfaceName));
    }

    LOG(ERROR) << "TransportFactory: unknown transport '" << name << "'";
    return nullptr;
}

} // namespace RSCGroup