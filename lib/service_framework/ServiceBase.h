#pragma once

#include <Startable.h>

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

private:
    void rollbackStartedTransports(std::size_t startedCount, IServiceTransport* currentTransport = nullptr) noexcept;
    void stopAllTransports() noexcept;

    std::string serviceName_;
    std::vector<std::shared_ptr<IServiceTransport>> transports_;
    bool running_{false};
    bool ready_{false};
};

} // namespace RSCGroup
