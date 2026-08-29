#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace RSCGroup {

/**
 * @brief Health state for a single named component.
 */
enum class HealthStatus {
    Ok,       ///< Component is healthy.
    Degraded, ///< Component is operational but impaired.
    Failed,   ///< Component has failed.
    Unknown,  ///< Health has not yet been reported.
};

/**
 * @brief A health record for one named component.
 */
struct ComponentHealth {
    std::string  component; ///< Component name (e.g. "inventory.loop").
    HealthStatus status{HealthStatus::Unknown};
    std::string  message;   ///< Human-readable detail (may be empty).
};

/**
 * @brief Centralised health tracker for a service.
 *
 * Components call report() to publish their health.  Callers (transports,
 * monitors) subscribe via addListener() to receive updates, or poll via
 * getHealth() / overallStatus().
 *
 * Thread safety: all public methods are thread-safe.
 */
class ServiceHealthTracker {
public:
    using Listener = std::function<void(const ComponentHealth&)>;

    ServiceHealthTracker() = default;

    /**
     * @brief Report the health of a named component.
     *
     * Creates a new record if @p component has not been seen before, or
     * updates the existing one.  Notifies all registered listeners after
     * each update.
     */
    void report(const std::string& component,
                HealthStatus       status,
                const std::string& message = {});

    /**
     * @brief Retrieve the last reported health for @p component.
     *
     * Returns ComponentHealth with HealthStatus::Unknown if the component
     * has never reported.
     */
    [[nodiscard]] ComponentHealth getHealth(const std::string& component) const;

    /**
     * @brief Returns the worst health status across all components.
     *
     * Failed > Degraded > Unknown > Ok.  Returns Ok if no components have
     * been registered.
     */
    [[nodiscard]] HealthStatus overallStatus() const;

    /**
     * @brief Register a listener that is called on every health change.
     *
     * Listeners are called synchronously from inside report(), while the
     * internal mutex is NOT held.
     */
    void addListener(Listener listener);

    /**
     * @brief Remove all registered listeners (useful in tests).
     */
    void clearListeners();

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ComponentHealth> records_;
    std::vector<Listener> listeners_;
};

} // namespace RSCGroup
