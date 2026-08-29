#pragma once

#include <string>

namespace RSCGroup {

/**
 * @brief Centralised glog initialisation and shutdown.
 *
 * Wraps the glog lifecycle so that every service initialises logging in an
 * identical, predictable way.  Call initialize() once from main() (or from
 * DaemonRunner) before any LOG() statements are executed.  Call shutdown()
 * at program exit, after all threads have stopped logging.
 */
class LoggingInitializer {
public:
    /**
     * @brief Initialise glog for the given application.
     *
     * Sets the program name used in log file names, applies @p logLevel as
     * the glog verbosity (VLOG threshold), and enables logging to stderr.
     *
     * @param appName  Argv[0] / binary name passed to InitGoogleLogging.
     * @param logLevel glog VLOG level (0 = INFO-only, higher = more verbose).
     */
    static void initialize(const std::string& appName, int logLevel = 0);

    /**
     * @brief Flush all pending log messages and shut down glog.
     *
     * Must be called once, after all threads have stopped issuing LOG()
     * statements, typically at the end of main().
     */
    static void shutdown();
};

} // namespace RSCGroup
