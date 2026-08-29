#pragma once

#include "IServiceConfig.h"

#include <string>

namespace RSCGroup {

/**
 * @brief IServiceConfig implementation backed by gflags.
 *
 * Reads values from the gflags registry.  If a flag with the requested name
 * has not been registered, or if its value cannot be retrieved, the caller-
 * supplied default is returned instead.
 *
 * Common flag names used across services:
 *   - "transport"        — transport type (dbus, stdout)
 *   - "transport_config" — transport-specific config string
 *   - "log_level"        — glog verbosity level
 *   - "reconcile_ms"     — full-refresh interval (inventory)
 *   - "min_refresh_ms"   — coalescing window (inventory)
 *   - "device_meta_path" — device metadata JSON path
 *   - "node_name_path"   — node name file path
 *   - "firmware_path"    — firmware version file path
 *   - "uuid_path"        — UUID file path
 *   - "software_path"    — software version file path
 */
class GflagsConfig : public IServiceConfig {
public:
    GflagsConfig() = default;

    /**
     * @brief Return the string value of gflag @p key, or @p defaultVal if the
     *        flag is not registered or has not been set.
     */
    std::string getString(const std::string& key,
                          const std::string& defaultVal) const override;

    /**
     * @brief Return the int32 value of gflag @p key, or @p defaultVal if the
     *        flag is not registered or has not been set.
     */
    int getInt(const std::string& key, int defaultVal) const override;

    /**
     * @brief Return the bool value of gflag @p key, or @p defaultVal if the
     *        flag is not registered or has not been set.
     */
    bool getBool(const std::string& key, bool defaultVal) const override;
};

} // namespace RSCGroup
