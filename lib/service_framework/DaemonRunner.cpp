#include "DaemonRunner.h"

#include <glog/logging.h>

namespace RSCGroup {

// static
int DaemonRunner::run(const std::string& appName,
                      Startable&         service,
                      const IServiceConfig& config,
                      SetupFn            setupFn)
{
    const int logLevel = config.getInt("log_level", 0);
    LoggingInitializer::initialize(appName, logLevel);

    daemon_support::installSignalHandlers();

    if (setupFn) {
        if (!setupFn()) {
            LOG(ERROR) << appName << ": pre-start setup failed";
            LoggingInitializer::shutdown();
            return 1;
        }
    }

    try {
        if (!service.start()) {
            LOG(ERROR) << appName << ": failed to start service '" << service.name() << "'";
            LoggingInitializer::shutdown();
            return 1;
        }
    } catch (const std::exception& e) {
        LOG(ERROR) << appName << ": exception during service start: " << e.what();
        LoggingInitializer::shutdown();
        return 1;
    } catch (...) {
        LOG(ERROR) << appName << ": unknown exception during service start";
        LoggingInitializer::shutdown();
        return 1;
    }

    LOG(INFO) << appName << " started";

    daemon_support::waitForShutdown();

    LOG(INFO) << appName << ": shutting down...";
    service.stop();
    LoggingInitializer::shutdown();
    return 0;
}

} // namespace RSCGroup
