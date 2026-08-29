#pragma once

#include "Startable.h"

#include <memory>
#include <string>
#include <vector>

namespace RSCGroup {

/**
 * @brief Manages an ordered set of Startable components with rollback semantics.
 *
 * Components are registered with add() and started in registration order when
 * start() is called.  On any failure, all components that were already started
 * are stopped in reverse order before start() returns false.
 *
 * stop() stops all running components in reverse registration order.  It is
 * idempotent: calling it when not running is a no-op.
 *
 * Thread safety: not thread-safe.  Callers must serialise calls to start(),
 * stop(), add(), and isRunning().
 */
class LifecycleManager {
public:
    LifecycleManager() = default;
    ~LifecycleManager() = default;

    LifecycleManager(const LifecycleManager&) = delete;
    LifecycleManager& operator=(const LifecycleManager&) = delete;

    /**
     * @brief Register a component.
     * @note Must be called before start().
     */
    void add(std::string name, std::shared_ptr<Startable> component);

    /**
     * @brief Start all components in registration order.
     *
     * On failure, already-started components are stopped in reverse order
     * before this function returns false.
     *
     * @return true if all components started successfully, false otherwise.
     */
    bool start();

    /**
     * @brief Stop all running components in reverse registration order.
     *
     * Idempotent: safe to call even if already stopped or never started.
     */
    void stop();

    [[nodiscard]] bool isRunning() const { return running_; }

private:
    struct Entry {
        std::string name;
        std::shared_ptr<Startable> component;
    };

    std::vector<Entry> components_;
    bool running_{false};
};

} // namespace RSCGroup
