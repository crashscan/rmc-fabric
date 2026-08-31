#pragma once

#include <ITypedTransport.h>
#include <string>

namespace RSCGroup {

class IInventoryQueryService;

/**
 * @brief Transport interface for the rmc-inventory service.
 *
 * Inherits ITypedTransport<IInventoryQueryService> which provides the
 * compile-time-verified bindQueryService() binding step.  Binding must
 * happen before start().
 */
class IInventoryTransport : public ITypedTransport<IInventoryQueryService> {
public:
    ~IInventoryTransport() override = default;

    virtual void start() = 0;
    virtual void stop() = 0;

    virtual void publishInventoryChanged(const std::string& fieldPath) = 0;
    virtual void publishSourceStateChanged(const std::string& sourceName) = 0;
    virtual void publishReadyChanged(bool ready) = 0;
};

// Backward-compatibility alias for in-tree migration. Prefer IInventoryTransport.
using ITransport = IInventoryTransport;

} // namespace RSCGroup