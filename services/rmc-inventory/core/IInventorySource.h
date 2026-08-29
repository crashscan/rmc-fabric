#pragma once

#include "InventoryTypes.h"

#include <string>

namespace RSCGroup {

class IInventorySource {
public:
    virtual ~IInventorySource() = default;

    [[nodiscard]] virtual std::string getName() const = 0;
    [[nodiscard]] virtual bool isRequired() const = 0;
    [[nodiscard]] virtual FieldNameList getOwnedFields() const = 0;
    [[nodiscard]] virtual InventoryFields collect() = 0;
    [[nodiscard]] virtual SourceState getState() const = 0;
};

} // namespace RSCGroup