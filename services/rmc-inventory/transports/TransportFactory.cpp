#include "TransportFactory.h"
#include "DbusInventoryTransport.h"
#ifdef INVENTORY_ENABLE_STDOUT_TRANSPORT
#include "StdoutInventoryTransport.h"
#endif

#include <inventory.hpp>
#include <glog/logging.h>

namespace RSCGroup {

std::shared_ptr<ITransport> TransportFactory::create(
    const std::string& name,
    std::shared_ptr<DBus::Connection> connection,
    std::string serviceName,
    std::string objectPath,
    std::string interfaceName)
{
#ifdef INVENTORY_ENABLE_STDOUT_TRANSPORT
    if (name == "stdout") {
        return std::make_shared<StdoutInventoryTransport>();
    }
#endif

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
