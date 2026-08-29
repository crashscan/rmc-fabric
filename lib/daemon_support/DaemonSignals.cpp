#include "DaemonSignals.h"

#include <atomic>
#include <csignal>
#include <thread>

namespace RSCGroup {
namespace daemon_support {

namespace {

// std::atomic<bool> is lock-free on all major platforms (guaranteed by
// is_always_lock_free == true for bool), making store() safe to call from a
// signal handler on those architectures.  This is the standard C++ approach
// for signal-to-main-thread coordination.
std::atomic<bool> g_shutdownRequested{false};

void handleSignal(int /*signum*/)
{
    g_shutdownRequested.store(true, std::memory_order_relaxed);
}

} // namespace

void installSignalHandlers()
{
    ::signal(SIGINT,  handleSignal);
    ::signal(SIGTERM, handleSignal);
}

void waitForShutdown(std::chrono::milliseconds pollInterval)
{
    while (!g_shutdownRequested.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(pollInterval);
    }
}

} // namespace daemon_support
} // namespace RSCGroup
