#pragma once

#include <chrono>

namespace RSCGroup {
namespace daemon_support {

/**
 * @brief Install SIGINT and SIGTERM handlers that mark a shutdown request.
 *
 * Must be called from main() before the service is started.  The handlers
 * are async-signal-safe: they set a volatile flag only.
 *
 * After installation, waitForShutdown() can be used to block the main thread
 * until one of these signals is received.
 */
void installSignalHandlers();

/**
 * @brief Block the calling thread until a shutdown signal has been received.
 *
 * Polls at @p pollInterval.  Returns as soon as SIGINT or SIGTERM has been
 * delivered (i.e. after installSignalHandlers() received a signal).
 *
 * @param pollInterval  How often to check the stop flag (default: 500 ms).
 */
void waitForShutdown(
    std::chrono::milliseconds pollInterval = std::chrono::milliseconds(500));

} // namespace daemon_support
} // namespace RSCGroup
