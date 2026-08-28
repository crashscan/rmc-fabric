#pragma once

#include "IInventorySource.h"
#include "InventoryFields.h"

#include <memory>
#include <vector>
#include <string>

#include <interop_contract/inventory/InventorySnapshot.hpp>
#include <interop_contract/inventory/InventorySourceState.hpp>

namespace RSCGroup {

class IInventoryManager {
public:
    virtual ~IInventoryManager() = default;

    virtual void addSource(std::shared_ptr<IInventorySource> source) = 0;
    virtual InventoryDiff refreshAll() = 0;

    [[nodiscard]] virtual interop_contract::inventory::InventorySnapshot getSnapshot() const = 0;
    [[nodiscard]] virtual interop_contract::inventory::SourceStateMap getSourceStates() const = 0;
    [[nodiscard]] virtual bool isReady() const = 0;
    [[nodiscard]] virtual std::string getPhase() const = 0;
    [[nodiscard]] virtual uint64_t getVersion() const = 0;
};

} // namespace RSCGroup