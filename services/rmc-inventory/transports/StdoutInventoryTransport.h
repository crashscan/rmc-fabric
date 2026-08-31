#pragma once

#include <IInventoryTransport.h>

#include <atomic>
#include <string>

namespace RSCGroup {

class IInventoryQueryService;

class StdoutInventoryTransport final : public IInventoryTransport {
public:
    void bindQueryService(IInventoryQueryService& queryService) override;
    [[nodiscard]] bool start() override;
    void stop() override;
    [[nodiscard]] std::string name() const override;

    void publishInventoryChanged(const std::string& fieldPath) override;
    void publishSourceStateChanged(const std::string& sourceName) override;
    void publishReadyChanged(bool ready) override;

private:
    void printField(const std::string& fieldPath) const;

    IInventoryQueryService* query_ = nullptr;
    std::atomic<bool> running_{false};
};

} // namespace RSCGroup
