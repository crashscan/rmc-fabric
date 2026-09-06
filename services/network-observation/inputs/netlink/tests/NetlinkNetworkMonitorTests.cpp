//
// Created by vvass on 06-Sep-26.
//
#include "NetlinkNetworkMonitor.h"

#include <UniqueFd.h>

#include <sys/socket.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

namespace {

using namespace RSCGroup;

constexpr auto testTimeout = std::chrono::seconds(5);
constexpr auto pollInterval = std::chrono::milliseconds(5);

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr
            << "NetlinkNetworkMonitorTests: "
            << message
            << '\n';

        std::exit(EXIT_FAILURE);
    }
}

struct SocketPair {
    UniqueFd monitor;
    UniqueFd peer;
};

[[nodiscard]] SocketPair makeStreamSocketPair()
{
    int descriptors[2]{-1, -1};

    if (::socketpair(
            AF_UNIX,
            SOCK_STREAM | SOCK_CLOEXEC,
            0,
            descriptors) < 0) {
        std::cerr
            << "NetlinkNetworkMonitorTests: socketpair failed: "
            << std::strerror(errno)
            << '\n';

        std::exit(EXIT_FAILURE);
    }

    return {
        UniqueFd(descriptors[0]),
        UniqueFd(descriptors[1]),
    };
}

[[nodiscard]] bool waitFor(
    const std::function<bool()>& predicate,
    std::chrono::steady_clock::duration timeout = testTimeout)
{
    const auto deadline =
        std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }

        std::this_thread::sleep_for(pollInterval);
    }

    return predicate();
}

void testWorkerDeathMakesMonitorUnhealthy()
{
    SocketPair sockets = makeStreamSocketPair();

    NetlinkNetworkMonitor monitor(
        sockets.monitor.get());

    expect(
        monitor.start(),
        "monitor should start with an injected live descriptor");

    expect(
        monitor.isRunning(),
        "monitor should initially report running");

    /*
     * NetlinkEventLoop polls sockets.monitor. Closing the connected peer
     * produces POLLHUP, causing the live loop to return a failure. The worker
     * converts that result into an exception and records workerFailed_ in its
     * exit handler.
     */
    sockets.peer.reset();

    expect(
        waitFor([&monitor] {
            return !monitor.isRunning();
        }),
        "unexpected worker termination should make isRunning() false");

    /*
     * Worker failure does not end the lifecycle epoch. Explicit stop is still
     * required to reap the failed worker, release resources, and permit a
     * future epoch.
     */
    monitor.stop();

    expect(
        !monitor.isRunning(),
        "monitor should remain stopped after explicit cleanup");

    /*
     * The injected descriptor is borrowed. Monitor destruction/stop must not
     * close it.
     */
    expect(
        sockets.monitor.valid(),
        "monitor must not take ownership of the injected descriptor");
}

void testInvalidInjectedDescriptorFailsStartup()
{
    NetlinkNetworkMonitor monitor(-1);

    expect(
        !monitor.start(),
        "negative injected descriptor should fail startup");

    expect(
        !monitor.isRunning(),
        "failed injected startup should leave monitor stopped");

    monitor.stop();
}

} // namespace

int main()
{
    testWorkerDeathMakesMonitorUnhealthy();
    testInvalidInjectedDescriptorFailsStartup();

    return EXIT_SUCCESS;
}