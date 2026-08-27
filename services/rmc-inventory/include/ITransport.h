#pragma once

#include <string>

namespace RSCGroup {

class IInventoryQueryService;

class ITransport {
public:
    virtual ~ITransport() = default;

    virtual void start(IInventoryQueryService& queryService) = 0;
    virtual void stop() = 0;

    virtual void publishInventoryChanged(const std::string& fieldPath) = 0;
    virtual void publishSourceStateChanged(const std::string& sourceName) = 0;
    virtual void publishReadyChanged(bool ready) = 0;
};

} // namespace RSCGroup
