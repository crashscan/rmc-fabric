#pragma once

#include <string>
#include <vector>

namespace RSCGroup {

class IFileWatcher {
public:
    virtual ~IFileWatcher() = default;

    virtual void watchPath(const std::string& path) = 0;

    [[nodiscard]] virtual int getPollFd() const = 0;

    // Performs non-event-driven watcher maintenance such as retrying
    // directory watch registration after invalidation or disappearance.
    virtual void maintain() = 0;

    // Drains pending events and returns changed watched paths.
    [[nodiscard]] virtual std::vector<std::string> consumeChangedPaths() = 0;
};

} // namespace RSCGroup
