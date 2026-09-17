#include "NetlinkNetworkMonitorFactory.h"

#include <UniqueFd.h>

#include <sys/socket.h>

#include <fcntl.h>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using namespace RSCGroup;
using RSCGroup::test_support::NetlinkNetworkMonitorFactory;

constexpr auto testTimeout = std::chrono::seconds(5);
constexpr auto pollInterval = std::chrono::milliseconds(5);

void expect(bool condition, const std::string &message) {
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

[[nodiscard]] bool descriptorIsOpen(int fd) {
    errno = 0;
    return ::fcntl(fd, F_GETFD) != -1 ||
           errno != EBADF;
}

[[nodiscard]] SocketPair makeStreamSocketPair() {
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
    const std::function<bool()> &predicate,
    std::chrono::steady_clock::duration timeout = testTimeout) {
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

void testWorkerDeathRequiresStopAndAllowsFreshDescriptorRestart() {
    SocketPair first = makeStreamSocketPair();
    SocketPair second = makeStreamSocketPair();

    const std::array<int, 2> liveDescriptors{
        first.monitor.get(),
        second.monitor.get(),
    };

    std::atomic<std::size_t> providerCalls{0};

    auto monitor = NetlinkNetworkMonitorFactory::create(
        [&]() -> int {
            const std::size_t index =
                    providerCalls.fetch_add(
                        1,
                        std::memory_order_relaxed);

            if (index >= liveDescriptors.size()) {
                throw std::runtime_error(
                    "unexpected additional live-FD request");
            }

            return liveDescriptors[index];
        });

    // First epoch.
    expect(
        monitor->start(),
        "monitor should start with the first injected descriptor");

    expect(
        providerCalls.load(std::memory_order_relaxed) == 1,
        "first startup should request exactly one descriptor");

    expect(
        monitor->isRunning(),
        "first monitor epoch should initially be healthy");

    first.peer.reset();

    expect(
        waitFor([&monitor] {
            return !monitor->isRunning();
        }),
        "first worker death should make the monitor unhealthy");

    /*
     * Worker failure changes health, not lifecycle state. Restart remains
     * rejected until explicit stop reaps the failed epoch.
     */
    expect(
        !monitor->start(),
        "restart before explicit stop should be rejected");

    expect(
        providerCalls.load(std::memory_order_relaxed) == 1,
        "rejected restart must not request another descriptor");

    monitor->stop();

    expect(
        !monitor->isRunning(),
        "explicit stop should clean up the first failed epoch");

    expect(
        descriptorIsOpen(first.monitor.get()),
        "monitor must not close the first borrowed descriptor");

    // Second epoch with a fresh descriptor.
    expect(
        monitor->start(),
        "monitor should restart with the second injected descriptor");

    expect(
        providerCalls.load(std::memory_order_relaxed) == 2,
        "restart should request exactly one fresh descriptor");

    expect(
        monitor->isRunning(),
        "second monitor epoch should initially be healthy");

    /*
     * Fail the second descriptor as well. This proves the restarted worker is
     * polling the descriptor returned for the second epoch.
     */
    second.peer.reset();

    expect(
        waitFor([&monitor] {
            return !monitor->isRunning();
        }),
        "second worker death should make the restarted monitor unhealthy");

    monitor->stop();

    expect(
        !monitor->isRunning(),
        "monitor should be stopped after second cleanup");

    expect(
        descriptorIsOpen(first.monitor.get()),
        "monitor must not close the second borrowed descriptor");

    expect(
        providerCalls.load(std::memory_order_relaxed) == 2,
        "two claimed startup epochs should request two descriptors");
}

void testInvalidProvidedDescriptorFailsStartup() {
    std::atomic<int> providerCalls{0};

    auto monitor = NetlinkNetworkMonitorFactory::create(
        [&] {
            providerCalls.fetch_add(
                1,
                std::memory_order_relaxed);

            return -1;
        });

    expect(
        !monitor->start(),
        "negative provided descriptor should fail startup");

    expect(
        !monitor->isRunning(),
        "failed injected startup should leave the monitor stopped");

    expect(
        providerCalls.load(std::memory_order_relaxed) == 1,
        "failed startup should invoke the provider exactly once");

    monitor->stop();
}

void testProviderExceptionRollsBackStartup() {
    std::atomic<int> providerCalls{0};

    auto monitor = NetlinkNetworkMonitorFactory::create(
        [&]() -> int {
            providerCalls.fetch_add(
                1,
                std::memory_order_relaxed);

            throw std::runtime_error(
                "injected descriptor provider failed");
        });

    expect(
        !monitor->start(),
        "provider exception should fail startup");

    expect(
        !monitor->isRunning(),
        "provider exception should leave the monitor stopped");

    expect(
        providerCalls.load(std::memory_order_relaxed) == 1,
        "provider should be invoked exactly once");

    monitor->stop();
}
} // namespace

int main() {
    testWorkerDeathRequiresStopAndAllowsFreshDescriptorRestart();
    testInvalidProvidedDescriptorFailsStartup();
    testProviderExceptionRollsBackStartup();

    return EXIT_SUCCESS;
}