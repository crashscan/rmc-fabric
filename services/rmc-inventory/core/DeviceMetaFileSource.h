#pragma once

#include "FileBackedInventorySource.h"

namespace RSCGroup {

class DeviceMetaFileSource : public FileBackedInventorySource {
public:
    explicit DeviceMetaFileSource(std::string filePath = "/data/info/device-meta.json",
                                  bool required = false);

protected:
    [[nodiscard]] InventoryFields fieldsFromContents(const std::string& contents) const override;
};

} // namespace RSCGroup