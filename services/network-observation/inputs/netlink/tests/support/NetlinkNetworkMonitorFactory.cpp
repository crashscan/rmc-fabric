#include "NetlinkNetworkMonitorFactory.h"

#include <stdexcept>
#include <utility>

namespace RSCGroup::test_support {
std::unique_ptr<NetlinkNetworkMonitor>
NetlinkNetworkMonitorFactory::create(
    LiveFdProvider liveFdProvider,
    MonitorCallbacks callbacks,
    std::set<std::string> watchedInterfaces) {
    if (!liveFdProvider) {
        throw std::invalid_argument(
            "NetlinkNetworkMonitorFactory: live FD provider is empty");
    }

    return std::unique_ptr<NetlinkNetworkMonitor>(
        new NetlinkNetworkMonitor(
            std::move(liveFdProvider),
            std::move(callbacks),
            std::move(watchedInterfaces)));
}
} // namespace RSCGroup::test_support