#pragma once

#include <inventory_service_api/ITransport.h>

#include <memory>
#include <string>

namespace DBus { class Connection; }

namespace RSCGroup {

class TransportFactory {
public:
    [[nodiscard]] static std::shared_ptr<ITransport> create(
        const std::string& name,
        std::shared_ptr<DBus::Connection> connection = nullptr,
        std::string serviceName   = {},
        std::string objectPath    = {},
        std::string interfaceName = {});
};

} // namespace RSCGroup