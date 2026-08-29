#pragma once

#include <memory>
#include <shared_mutex>

namespace RSCGroup {

/**
 * @brief Thread-safe lifetime manager for a nullable query-service pointer.
 *
 * Ownership invariant
 * -------------------
 * The service object is externally owned (typically by the daemon runner).
 * ServiceBinding does NOT own it.  bind() registers the pointer for use by
 * D-Bus handler callbacks; detach() atomically revokes access and waits for
 * all in-flight calls to complete before returning.
 *
 * Thread-safety
 * -------------
 * Multiple D-Bus dispatcher threads may call acquire() concurrently.
 * detach() is called from the transport lifecycle thread (onTransportStopping).
 *
 * Implementation uses std::shared_mutex:
 *  - acquire() takes a *shared* lock for the duration of the service call.
 *  - detach() takes a *unique* lock, which blocks until all shared holders
 *    have released.  This guarantees that after detach() returns, no handler
 *    can access the service pointer.
 *
 * Deadlock analysis
 * -----------------
 * D-Bus handler methods call into the service but never call back into the
 * transport layer, so the shared lock cannot be re-entered or cause a cycle.
 */
template <typename T>
class ServiceBinding {
public:
    ServiceBinding() = default;

    // Non-copyable, non-movable (mutex and pointer must have stable addresses)
    ServiceBinding(const ServiceBinding&) = delete;
    ServiceBinding& operator=(const ServiceBinding&) = delete;

    /**
     * @brief Register the service pointer.
     *
     * Must be called *before* D-Bus handler methods can be dispatched
     * (i.e., before the adapter's bind() step that registers methods on the
     * bus object).  Calling bind() while handler threads hold shared locks
     * via acquire() will stall until those locks are released — this is safe
     * but callers should avoid it by following the start-order contract:
     *   setService() → DbusTransportBase::start() (which calls adapter bind()).
     */
    void bind(T* service) noexcept
    {
        std::unique_lock lock(mtx_);
        service_ = service;
    }

    /**
     * @brief Clear the service pointer and wait for all in-flight calls to
     *        complete.  After this call returns, acquire() will always return
     *        a null Guard.
     */
    void detach() noexcept
    {
        std::unique_lock lock(mtx_);
        service_ = nullptr;
    }

    /**
     * @brief RAII guard returned by acquire().
     *
     * Holds the shared lock for its lifetime.  The service pointer is valid
     * as long as the Guard is alive.  A default-constructed (or null) Guard
     * has get() == nullptr.
     */
    class Guard {
    public:
        Guard() noexcept = default;

        Guard(std::shared_lock<std::shared_mutex> lock, T* ptr) noexcept
            : lock_(std::move(lock)), ptr_(ptr)
        {}

        [[nodiscard]] T* get()    const noexcept { return ptr_; }
        explicit operator bool()  const noexcept { return ptr_ != nullptr; }
        T* operator->()           const noexcept { return ptr_; }

    private:
        std::shared_lock<std::shared_mutex> lock_;
        T* ptr_ = nullptr;
    };

    /**
     * @brief Try to pin the service for one call.
     *
     * Returns a Guard that holds the service alive for the duration of the
     * call.  Returns a null Guard if the binding has been detached.
     *
     * Usage:
     * @code
     *   if (auto guard = binding_.acquire()) {
     *       return guard->someMethod();
     *   }
     *   return defaultValue;
     * @endcode
     */
    [[nodiscard]] Guard acquire()
    {
        std::shared_lock lock(mtx_);
        if (!service_) return {};
        return Guard(std::move(lock), service_);
    }

private:
    mutable std::shared_mutex mtx_;
    T* service_ = nullptr;
};

} // namespace RSCGroup
