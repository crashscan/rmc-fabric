#pragma once

#include "inventory_core/FileBackedInventorySource.h"

namespace RSCGroup {

class ScalarFileSource : public FileBackedInventorySource {
public:
    ScalarFileSource(std::string name, bool required, std::string filePath, std::string fieldName)
        : FileBackedInventorySource(std::move(name), required, std::move(filePath), std::vector{std::move(fieldName)})
        , fieldName_(getOwnedFields().front())
    {}

protected:
    [[nodiscard]] InventoryFields fieldsFromContents(const std::string& contents) const override
    {
        return {{fieldName_, scalarFromContents(contents, getName())}};
    }

private:
    std::string fieldName_;
};

} // namespace RSCGroup