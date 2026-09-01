#pragma once

#include <string>

namespace RSCGroup {

/**
 * @brief Minimal interface for components that have a start/stop lifecycle.
 *
 * `ServiceBase` implements `Startable`, and `DaemonRunner` wraps process
 * lifecycle around one.  Startable carries no orchestration policy of its own:
 * service-epoch serialization belongs to `LifecycleCoordinator` and
 * worker-thread mechanics belong to `ManagedWorker`.
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
