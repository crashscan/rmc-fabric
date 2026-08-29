#pragma once

#include "IServiceConfig.h"
#include "LoggingInitializer.h"

#include <DaemonSignals.h>
#include <Startable.h>

#include <functional>
#include <memory>
#include <string>

namespace RSCGroup {

/**
 * @brief Standardised daemon entry-point framework.
 *
 * DaemonRunner handles the boilerplate common to every service daemon:
 *   1. Logging initialisation (via LoggingInitializer).
 *   2. Signal handler installation (SIGINT / SIGTERM).
 *   3. Service startup (via Startable).
 *   4. Blocking until a shutdown signal is received.
 *   5. Ordered teardown and logging shutdown.
 *
 * Services that have any pre-start or post-start setup that does not fit
 * into initializeComponents() can supply a @p setupFn callback that runs
 * between logging init and service start.
 *
 * Usage:
 * @code
 *   int main(int argc, char* argv[]) {
 *       gflags::ParseCommandLineFlags(&argc, &argv, true);
 *       GflagsConfig cfg;
 *       MyService service(cfg);
 *       return DaemonRunner::run(argv[0], service, cfg);
 *   }
 * @endcode
 */
class DaemonRunner {
public:
    /**
     * @brief Optional setup callback invoked after logging is initialised but
     *        before service.start().  Return false to abort startup.
     */
    using SetupFn = std::function<bool()>;

    /**
     * @brief Execute the full daemon lifecycle.
     *
     * @param appName  Passed to LoggingInitializer::initialize().
     * @param service  Service to start/stop.
     * @param config   Configuration source (queried for "log_level").
     * @param setupFn  Optional pre-start callback (default: no-op).
     * @return 0 on clean shutdown; 1 if startup failed.
     */
    static int run(const std::string& appName,
                   Startable&         service,
                   const IServiceConfig& config,
                   SetupFn            setupFn = {});
};

} // namespace RSCGroup
