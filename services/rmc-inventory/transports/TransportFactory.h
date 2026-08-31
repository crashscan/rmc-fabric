#pragma once

#include <ITransport.h>

#include <memory>
#include <string>

namespace DBus { class Connection; }

namespace RSCGroup {

class TransportFactory {
public:
    [[nodiscard]] static std::shared_ptr<IInventoryTransport> create(
        const std::string& name,
        std::shared_ptr<DBus::Connection> connection = nullptr,
        std::string serviceName   = {},
        std::string objectPath    = {},
        std::string interfaceName = {});
};

} // namespace RSCGroup
