#pragma once

#include "NetlinkNetworkMonitor.h"

#include <functional>
#include <memory>
#include <set>
#include <string>

namespace RSCGroup::test_support {

/**
 * Test-only construction support for NetlinkNetworkMonitor.
 *
 * This header is private to the netlink test target and is not installed or
 * exported. The provider is called once for each successfully claimed startup
 * epoch. Each returned descriptor is borrowed; the test retains ownership.
 */
class NetlinkNetworkMonitorFactory {
public:
    using LiveFdProvider = std::function<int()>;

    [[nodiscard]] static std::unique_ptr<NetlinkNetworkMonitor>
    create(
        LiveFdProvider liveFdProvider,
        MonitorCallbacks callbacks = {},
        std::set<std::string> watchedInterfaces = {});
};

} // namespace RSCGroup::test_support