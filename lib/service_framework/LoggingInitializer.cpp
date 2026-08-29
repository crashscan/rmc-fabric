#include "LoggingInitializer.h"

#include <glog/logging.h>

namespace RSCGroup {

void LoggingInitializer::initialize(const std::string& appName, int logLevel)
{
    google::InitGoogleLogging(appName.c_str());
    FLAGS_v = logLevel;
    FLAGS_logtostderr = true;
}

void LoggingInitializer::shutdown()
{
    google::ShutdownGoogleLogging();
}

} // namespace RSCGroup
