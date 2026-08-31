#pragma once

#include <condition_variable>
#include <mutex>

namespace RSCGroup {

/**
 * @brief Thread-safe, starvation-free lifetime manager for a nullable
 *        query-service pointer.
 *
 * Ownership invariant
 * -------------------
 * The service object is externally owned (typically by the daemon runner).
 * ServiceBinding does NOT own it.  bind() registers the pointer and opens
 * admission; detach() closes admission and drains in-flight calls before
 * clearing the pointer and returning.
 *
 * Thread-safety
 * -------------
 * Multiple D-Bus dispatcher threads may call acquire() concurrently.
 * detach() is called from the transport lifecycle thread (quiesceQueries /
 * onTransportStopping).
 *
 * Admission model
 * ---------------
 * An explicit admitting_ flag and activeCount_ integer replace the shared
 * mutex approach.  This prevents a continuous stream of new readers from
 * starving detach():
 *  - acquire() checks admitting_ and, if open, increments activeCount_.
 *  - detach() closes admission (admitting_ = false) before waiting, so
 *    no new acquires are accepted and activeCount_ can only decrease.
 *  - The returned Guard holds a back-pointer; its destructor decrements
 *    activeCount_ and notifies the condition variable.
 *
 * Deadlock analysis
 * -----------------
 * D-Bus handler methods call into the service but never call back into the
 * transport layer, so the guard cannot be re-entered or cause a cycle.
 * detach() releases the mutex before waiting — no mutex is held during the
 * drain wait.
 */
template <typename T>
class ServiceBinding {
public:
    ServiceBinding() = default;

    // Non-copyable, non-movable (mutex and pointer must have stable addresses)
    ServiceBinding(const ServiceBinding&) = delete;
    ServiceBinding& operator=(const ServiceBinding&) = delete;

    // ------------------------------------------------------------------ //
    // RAII guard
    // ------------------------------------------------------------------ //

    /**
     * @brief RAII guard returned by acquire().
     *
     * Holds an active-call lease for its lifetime.  The service pointer is
     * valid as long as the Guard is alive.  A default-constructed (null)
     * Guard has get() == nullptr and does not hold a lease.
     *
     * Move-only: copy is deleted so lease semantics are unambiguous.
     */
    class Guard {
    public:
        Guard() noexcept = default;

        Guard(ServiceBinding* owner, T* ptr) noexcept
            : owner_(owner), ptr_(ptr)
        {}

        ~Guard() noexcept { release(); }

        Guard(Guard&& o) noexcept
            : owner_(o.owner_), ptr_(o.ptr_)
        {
            o.owner_ = nullptr;
            o.ptr_   = nullptr;
        }

        Guard& operator=(Guard&& o) noexcept
        {
            if (this != &o) {
                release();
                owner_ = o.owner_;
                ptr_   = o.ptr_;
                o.owner_ = nullptr;
                o.ptr_   = nullptr;
            }
            return *this;
        }

        Guard(const Guard&)            = delete;
        Guard& operator=(const Guard&) = delete;

        [[nodiscard]] T* get()   const noexcept { return ptr_; }
        explicit operator bool() const noexcept { return ptr_ != nullptr; }
        T* operator->()          const noexcept { return ptr_; }

    private:
        void release() noexcept
        {
            if (!owner_) return;
            {
                std::unique_lock lk(owner_->mtx_);
                --owner_->activeCount_;
            }
            owner_->cv_.notify_all();
            owner_ = nullptr;
            ptr_   = nullptr;
        }

        ServiceBinding* owner_ = nullptr;
        T*              ptr_   = nullptr;
    };

    // ------------------------------------------------------------------ //
    // Lifecycle operations
    // ------------------------------------------------------------------ //

    /**
     * @brief Register the service pointer and open admission for new calls.
     *
     * Must be called before D-Bus handler methods can be dispatched.
     * Typically: setService() → DbusTransportBase::start() (which calls
     * adapter bind()).
     */
    void bind(T* service) noexcept
    {
        {
            std::unique_lock lk(mtx_);
            service_   = service;
            admitting_ = (service != nullptr);
        }
        cv_.notify_all();
    }

    /**
     * @brief Close admission and wait for all in-flight calls to drain.
     *
     * Postcondition: no active lease exists; acquire() will return a null
     * Guard until bind() is called again.
     *
     * detach() does NOT hold the internal mutex while waiting for the drain,
     * so it cannot deadlock with handlers that call back into service code.
     */
    void detach() noexcept
    {
        std::unique_lock lk(mtx_);
        admitting_ = false;
        cv_.wait(lk, [this] { return activeCount_ == 0; });
        service_ = nullptr;
    }

    /**
     * @brief Try to pin the service for one call.
     *
     * Returns a Guard that keeps the lease alive for the duration of the
     * call.  Returns a null Guard if admission is closed (detach in
     * progress or bind not yet called).
     *
     * @code
     *   if (auto guard = binding_.acquire()) {
     *       return guard->someMethod();
     *   }
     *   return defaultValue;
     * @endcode
     */
    [[nodiscard]] Guard acquire()
    {
        std::unique_lock lk(mtx_);
        if (!admitting_ || !service_) return {};
        ++activeCount_;
        return Guard(this, service_);
    }

private:
    mutable std::mutex      mtx_;
    std::condition_variable cv_;
    bool                    admitting_   = false;
    int                     activeCount_ = 0;
    T*                      service_     = nullptr;
};

} // namespace RSCGroup
