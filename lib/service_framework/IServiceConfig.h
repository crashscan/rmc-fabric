#pragma once

#include <string>

namespace RSCGroup {

/**
 * @brief Abstraction layer for service configuration.
 *
 * Decouples service components from a concrete configuration mechanism
 * (gflags, config files, environment variables, tests, etc.).
 *
 * Implementations provide type-safe access with a default fallback value
 * for every key that is not explicitly set.
 */
class IServiceConfig {
public:
    virtual ~IServiceConfig() = default;

    /**
     * @brief Retrieve a string configuration value.
     * @param key        Configuration key name.
     * @param defaultVal Value returned when the key is not set.
     */
    virtual std::string getString(const std::string& key,
                                  const std::string& defaultVal) const = 0;

    /**
     * @brief Retrieve an integer configuration value.
     * @param key        Configuration key name.
     * @param defaultVal Value returned when the key is not set.
     */
    virtual int getInt(const std::string& key, int defaultVal) const = 0;

    /**
     * @brief Retrieve a boolean configuration value.
     * @param key        Configuration key name.
     * @param defaultVal Value returned when the key is not set.
     */
    virtual bool getBool(const std::string& key, bool defaultVal) const = 0;
};

} // namespace RSCGroup
