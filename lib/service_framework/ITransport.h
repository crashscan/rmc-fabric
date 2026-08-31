#pragma once

#include <string>

namespace RSCGroup {

/**
 * @brief Generic transport interface for publishing service state changes
 *        to subscribers (D-Bus, stdout, …).
 *
 * Concrete transports implement start()/stop() for connection management
 * and the publish*() family for pushing events to consumers.
 *
 * Lifecycle:
 *  - start() — open the transport channel; return false on failure.
 *  - stop()  — close the transport channel; must be idempotent.
 *
 * The default no-op implementations of publishReadyChanged() allow
 * transports to opt-in only to the events they need.
 *
 * Service-specific transport types (e.g. IInventoryTransport) may extend
 * this interface with additional publish methods.
 */
class IServiceTransport {
public:
    virtual ~IServiceTransport() = default;

    /**
     * @brief Open the transport channel.
     * @return true on success; false if the transport could not start.
     */
    virtual bool start() = 0;

    /**
     * @brief Close the transport channel. Must be idempotent.
     */
    virtual void stop() = 0;

    /**
     * @brief Close query admission and wait for all in-flight query handlers
     *        to complete before returning.
     *
     * Postconditions (when this method returns):
     *  - no new externally-initiated query call is admitted;
     *  - all previously admitted query calls have returned;
     *  - publication resources (signals) remain open;
     *  - stop() remains safe to call without an explicit prior quiesceQueries().
     *
     * Default is a no-op for transports that have no query methods.
     * Failure to quiesce is a safety-barrier failure: implementations should
     * log or throw rather than silently continue.
     */
    virtual void quiesceQueries() {}

    /**
     * @brief Notify subscribers that the service ready state has changed.
     *
     * Default is a no-op so that transports that do not need ready-state
     * events can omit the override.  Transports that do publish this event
     * must override and emit the notification to their subscribers.
     */
    virtual void publishReadyChanged(bool ready) {}

    /**
     * @brief Human-readable transport name, used in log messages.
     */
    [[nodiscard]] virtual std::string name() const = 0;
};

} // namespace RSCGroup
