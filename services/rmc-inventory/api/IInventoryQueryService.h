#pragma once

#include <InventoryFields.h>

#include <interop_contract/inventory/InventorySnapshot.hpp>
#include <interop_contract/inventory/InventoryIssues.hpp>
#include <interop_contract/inventory/InventorySourceState.hpp>

#include <map>
#include <string>

namespace RSCGroup {

class IInventoryQueryService {
public:
    virtual ~IInventoryQueryService() = default;

    [[nodiscard]] virtual interop_contract::inventory::InventorySnapshot getIdentity() const = 0;

    [[nodiscard]] virtual InventoryFields getField(const std::string& fieldName) const = 0;

    [[nodiscard]] virtual interop_contract::inventory::SourceStateMap getSourceStates() const = 0;

    [[nodiscard]] virtual interop_contract::inventory::InventoryIssues getIssues() const = 0;

    [[nodiscard]] virtual bool getReady() const = 0;
    [[nodiscard]] virtual std::string getPhase() const = 0;
    [[nodiscard]] virtual uint64_t getVersion() const = 0;

    virtual void refresh() = 0;
};

} // namespace RSCGroup