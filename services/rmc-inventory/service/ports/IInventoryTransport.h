#pragma once

#include <ITransport.h>

#include <string>

namespace RSCGroup {

class IInventoryQueryService;

class IInventoryTransport : public IServiceTransport {
public:
    ~IInventoryTransport() override = default;

    virtual void bindQueryService(IInventoryQueryService& service) = 0;

    virtual void publishInventoryChanged(const std::string& fieldPath) = 0;
    virtual void publishSourceStateChanged(const std::string& sourceName) = 0;
};

} // namespace RSCGroup
