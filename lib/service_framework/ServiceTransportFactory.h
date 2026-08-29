#pragma once

#include "ITransport.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace RSCGroup {

/**
 * @brief Registry-based factory for IServiceTransport instances.
 *
 * Services register builder functions with a string key (e.g. "dbus",
 * "stdout") and later create transports by that key.  This eliminates
 * hard-coded if/else chains in service main() functions and makes it easy
 * to add new transport types without touching service code.
 *
 * Typical service-level registration:
 * @code
 *   ServiceTransportFactory::registerBuilder("dbus",
 *       [&](const std::string& config) {
 *           return std::make_shared<MyDbusTransport>(config);
 *       });
 *   ServiceTransportFactory::registerBuilder("stdout",
 *       [](const std::string&) {
 *           return std::make_shared<MyStdoutTransport>();
 *       });
 * @endcode
 *
 * Transport creation:
 * @code
 *   auto t = ServiceTransportFactory::create("dbus", "system");
 * @endcode
 *
 * Thread safety: registration and creation are not thread-safe. Builders
 * must be registered before any concurrent create() calls.
 */
class ServiceTransportFactory {
public:
    /**
     * @brief Builder function type: receives an opaque config string and
     *        returns a constructed (but not yet started) transport, or nullptr
     *        on failure.
     */
    using Builder = std::function<std::shared_ptr<IServiceTransport>(const std::string& config)>;

    /**
     * @brief Register a transport builder under @p name.
     *
     * Replaces any previously registered builder for the same name.
     *
     * @param name    Transport type name (e.g. "dbus", "stdout").
     * @param builder Builder function.
     */
    static void registerBuilder(const std::string& name, Builder builder);

    /**
     * @brief Create a transport by name.
     *
     * @param name   Transport type name, as registered with registerBuilder().
     * @param config Opaque config string forwarded verbatim to the builder.
     * @return A new transport instance, or nullptr if the name is unknown.
     */
    [[nodiscard]] static std::shared_ptr<IServiceTransport> create(
        const std::string& name, const std::string& config = {});

    /**
     * @brief Check whether a builder is registered for @p name.
     */
    [[nodiscard]] static bool hasBuilder(const std::string& name);

    /**
     * @brief Remove all registered builders (useful in unit tests).
     */
    static void clear();

private:
    static std::unordered_map<std::string, Builder>& registry();
};

} // namespace RSCGroup
