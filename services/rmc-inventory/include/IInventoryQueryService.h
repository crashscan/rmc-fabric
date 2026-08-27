#pragma once

#include <inventory_core/InventoryFields.h>
#include "interop_contract/inventory/InventorySnapshot.hpp"
#include "interop_contract/inventory/InventoryIssues.hpp"
#include "interop_contract/inventory/InventorySourceState.hpp"
#include <map>

namespace RSCGroup {

class IInventoryQueryService {
public:
    virtual ~IInventoryQueryService() = default;

    // Full identity snapshot returned as source-owned fields plus metadata.
    [[nodiscard]] virtual interop_contract::inventory::InventorySnapshot getIdentity() const = 0;

    // Returns a single-field map if present, otherwise empty.
    [[nodiscard]] virtual InventoryFields getField(const std::string& fieldName) const = 0;

    // Bulk source state query: sourceName -> state
    [[nodiscard]] virtual interop_contract::inventory::SourceStateMap getSourceStates() const = 0;

    /// Curated operator-facing view: active problems only, one per source.
    [[nodiscard]] virtual interop_contract::inventory::InventoryIssues getIssues() const = 0;

    [[nodiscard]] virtual bool getReady() const = 0;
    [[nodiscard]] virtual std::string getPhase() const = 0;
    [[nodiscard]] virtual uint64_t getVersion() const = 0;

    // Async/coalesced request
    virtual void refresh() = 0;
};

} // namespace RSCGroup
