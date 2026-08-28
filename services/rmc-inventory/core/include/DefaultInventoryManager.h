#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "IInventoryManager.h"
#include "IInventorySource.h"
#include "InventoryFields.h"

namespace RSCGroup {

class DefaultInventoryManager final : public IInventoryManager {
public:
    DefaultInventoryManager() = default;
    ~DefaultInventoryManager() override = default;

    DefaultInventoryManager(const DefaultInventoryManager&) = delete;
    DefaultInventoryManager& operator=(const DefaultInventoryManager&) = delete;

    void addSource(std::shared_ptr<IInventorySource> source) override;
    [[nodiscard]] InventoryDiff refreshAll() override;

    [[nodiscard]] interop_contract::inventory::InventorySnapshot getSnapshot() const override;
    [[nodiscard]] interop_contract::inventory::SourceStateMap getSourceStates() const override;
    [[nodiscard]] bool isReady() const override;
    [[nodiscard]] std::string getPhase() const override;
    [[nodiscard]] uint64_t getVersion() const override;

private:
    void validateSourceRegistration(const std::shared_ptr<IInventorySource>& source);

    [[nodiscard]] InventoryDiff diffSourceOwnedFields(const InventoryFields& oldFields,
                                                      const InventoryFields& newFields) const;

    [[nodiscard]] bool allRequiredSourcesOk(const interop_contract::inventory::SourceStateMap& states) const;

    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<IInventorySource>> sources_;
    std::map<std::string, std::string> fieldOwners_;
    interop_contract::inventory::InventorySnapshot snapshot_{};
    interop_contract::inventory::SourceStateMap sourceStates_;
};

} // namespace RSCGroup