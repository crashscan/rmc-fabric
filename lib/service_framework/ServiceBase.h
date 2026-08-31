#pragma once

#include <Startable.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace RSCGroup {

// Forward declaration: callers must include the appropriate transport header
// for the concrete type they wish to register.
class IServiceTransport;

/**
 * @brief Foundation class for all RMC services.
 *
 * ServiceBase implements the Startable interface and provides:
 *  - Ordered transport management (addTransport / transports access).
 *  - Ready-state tracking (isReady / setReady).
 *  - Two mandatory extension points for subclasses:
 *      initializeComponents() — register adapters, sources, transports, etc.
 *      validateConfiguration() — assert that all required config is present.
 *
 * Typical usage:
 * @code
 *   class MyService : public ServiceBase {
 *   public:
 *       explicit MyService() : ServiceBase("my-service") {}
 *   protected:
 *       bool initializeComponents() override { ... return true; }
 *       void validateConfiguration() override { ... }
 *   };
 * @endcode
 *
 * start() calls validateConfiguration(), then initializeComponents(), then
 * starts all registered transports in registration order.  On any failure
 * already-started transports are stopped in reverse order before start()
 * returns false.
 *
 * stop() stops all running transports in reverse registration order and
 * clears the ready flag.
 *
 * Thread safety: not thread-safe. Callers must serialise start/stop calls.
 */
class ServiceBase : public Startable {
public:
    explicit ServiceBase(const std::string& serviceName);
    ~ServiceBase() override;

    ServiceBase(const ServiceBase&) = delete;
    ServiceBase& operator=(const ServiceBase&) = delete;

    // ------------------------------------------------------------------ //
    // Startable interface
    // ------------------------------------------------------------------ //
    /**
     * @brief Validate configuration, initialise components, and start
     *        all registered transports in registration order.
     *
     * Rolls back (stops already-started transports) on any failure.
     *
     * @return true if all transports started successfully; false otherwise.
     */
    bool start() override;

    /**
     * @brief Stop all running transports in reverse registration order and
     *        clear the ready flag.  Idempotent.
     */
    void stop() override;

    /**
     * @brief Human-readable service name.
     */
    [[nodiscard]] std::string name() const override;

    // ------------------------------------------------------------------ //
    // Service-specific extension points
    // ------------------------------------------------------------------ //
    /**
     * @brief Initialise service components (sources, adapters, transports…).
     *
     * Called by start() after validateConfiguration() succeeds.  Subclasses
     * should register all transports via addTransport() inside this method
     * (or before calling start() — both approaches are supported).
     *
     * @return true if initialisation succeeded; false to abort startup.
     */
    virtual bool initializeComponents() = 0;

    /**
     * @brief Assert that all required configuration is present.
     *
     * Called by start() before initializeComponents().  Implementations
     * should throw std::invalid_argument (or a derived exception) if any
     * required configuration is missing or invalid.
     */
    virtual void validateConfiguration() = 0;

    // ------------------------------------------------------------------ //
    // Ready-state management
    // ------------------------------------------------------------------ //
    [[nodiscard]] bool isReady() const;
    [[nodiscard]] bool isRunning() const;

    /**
     * @brief Update the ready flag and notify all transports.
     *
     * Calls transport->publishReadyChanged(ready) for each registered
     * transport when the value changes.
     *
     * setReady(true) is rejected after shutdown has been claimed (i.e. after
     * stop() begins) and the call is a no-op.  This prevents event producers
     * draining after shutdown from re-asserting readiness.
     */
    void setReady(bool ready);

    // ------------------------------------------------------------------ //
    // Transport management
    // ------------------------------------------------------------------ //
    /**
     * @brief Register a transport. Can be called before or during
     *        initializeComponents().
     */
    void addTransport(std::shared_ptr<IServiceTransport> transport);

    /**
     * @brief Read-only access to the registered transports.
     */
    [[nodiscard]] const std::vector<std::shared_ptr<IServiceTransport>>& transports() const;

protected:
    /**
     * @brief Quiesce query admission on all registered transports.
     *
     * Iterates transports in registration order and calls
     * transport->quiesceQueries() on each.  A failure is a safety-barrier
     * failure and is re-thrown after logging; it must not be silently
     * swallowed.
     *
     * Callers must not hold the service lifecycle mutex while calling this,
     * as each quiesce may block waiting for in-flight handlers to drain.
     *
     * Postcondition: no new externally-initiated query is admitted; all
     * previously admitted query calls have returned.
     */
    void quiesceQueriesOnTransports();

    /**
     * @brief Stop all registered transports without touching the ready state.
     *
     * This is the "final close" step after ReadyChanged(false) has been
     * emitted.  Called by stop() and available to subclasses that need
     * fine-grained shutdown ordering.
     */
    void stopTransports() noexcept;

private:
    void rollbackStartedTransports(std::size_t startedCount, IServiceTransport* currentTransport = nullptr) noexcept;
    void stopAllTransports() noexcept;

    std::string serviceName_;
    std::vector<std::shared_ptr<IServiceTransport>> transports_;
    std::atomic<bool> running_{false};
    std::atomic<bool> ready_{false};
    /// Set at the start of stop() to block subsequent setReady(true) calls.
    std::atomic<bool> shutdownClaimed_{false};
};

} // namespace RSCGroup
