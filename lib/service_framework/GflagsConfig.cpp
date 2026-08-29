#include "GflagsConfig.h"

#include <gflags/gflags.h>

#include <string>

namespace RSCGroup {

std::string GflagsConfig::getString(const std::string& key,
                                    const std::string& defaultVal) const
{
    gflags::CommandLineFlagInfo info;
    if (!gflags::GetCommandLineFlagInfo(key.c_str(), &info)) {
        return defaultVal;
    }
    if (info.type != "string") {
        return defaultVal;
    }
    return info.current_value;
}

int GflagsConfig::getInt(const std::string& key, int defaultVal) const
{
    gflags::CommandLineFlagInfo info;
    if (!gflags::GetCommandLineFlagInfo(key.c_str(), &info)) {
        return defaultVal;
    }
    if (info.type != "int32") {
        // Only int32 flags are safely representable as int.
        // int64 flags may exceed int range; callers should use a wider type.
        return defaultVal;
    }
    try {
        return std::stoi(info.current_value);
    } catch (...) {
        return defaultVal;
    }
}

bool GflagsConfig::getBool(const std::string& key, bool defaultVal) const
{
    gflags::CommandLineFlagInfo info;
    if (!gflags::GetCommandLineFlagInfo(key.c_str(), &info)) {
        return defaultVal;
    }
    if (info.type != "bool") {
        return defaultVal;
    }
    return (info.current_value == "true");
}

} // namespace RSCGroup
