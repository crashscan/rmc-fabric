#pragma once

#include <ITransport.h>

#include <atomic>

namespace RSCGroup {

class IInventoryQueryService;

class StdoutInventoryTransport final : public ITransport {
public:
    void start(IInventoryQueryService& queryService) override;
    void stop() override;

    void publishInventoryChanged(const std::string& fieldPath) override;
    void publishSourceStateChanged(const std::string& sourceName) override;
    void publishReadyChanged(bool ready) override;

private:
    void printField(const std::string& fieldPath) const;

    IInventoryQueryService* query_ = nullptr;
    std::atomic<bool> running_{false};
};

} // namespace RSCGroup
