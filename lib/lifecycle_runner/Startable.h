#pragma once

#include <string>

namespace RSCGroup {

/**
 * @brief Minimal interface for components that have a start/stop lifecycle.
 *
 * LifecycleManager operates on Startable components: it starts them in
 * registration order and stops them in reverse order, rolling back on failure.
 */
class Startable {
public:
    virtual ~Startable() = default;

    /**
     * @brief Start the component.
     * @return true on success; false if the component could not start.
     */
    virtual bool start() = 0;

    /**
     * @brief Stop the component.  Must be idempotent and noexcept-safe.
     */
    virtual void stop() = 0;

    /**
     * @brief Human-readable component name, used in log messages.
     */
    [[nodiscard]] virtual std::string name() const = 0;
};

} // namespace RSCGroup
