#include "DaemonSignals.h"

#include <csignal>
#include <thread>

namespace RSCGroup {
namespace daemon_support {

namespace {

volatile sig_atomic_t g_shutdownRequested = 0;

void handleSignal(int /*signum*/)
{
    g_shutdownRequested = 1;
}

} // namespace

void installSignalHandlers()
{
    ::signal(SIGINT,  handleSignal);
    ::signal(SIGTERM, handleSignal);
}

void waitForShutdown(std::chrono::milliseconds pollInterval)
{
    while (!g_shutdownRequested) {
        std::this_thread::sleep_for(pollInterval);
    }
}

} // namespace daemon_support
} // namespace RSCGroup
