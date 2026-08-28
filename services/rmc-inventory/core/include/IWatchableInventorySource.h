#pragma once

#include <string>

namespace RSCGroup {

class IWatchableInventorySource {
public:
    virtual ~IWatchableInventorySource() = default;
    [[nodiscard]] virtual std::string getWatchPath() const = 0;
};

} // namespace RSCGroup