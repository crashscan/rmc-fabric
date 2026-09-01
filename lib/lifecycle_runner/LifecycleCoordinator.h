#pragma once

#include <condition_variable>
#include <mutex>

namespace RSCGroup {

/**
 * @brief Serializes complete service-epoch lifecycle transitions.
 *
 * `LifecycleCoordinator` owns **transition serialization only**: the
 * `stopped → starting → running → stopping → stopped` epoch machine,
 * coordination of concurrent lifecycle calls, and RAII completion of an
 * in-flight transition.  It knows nothing about transports, readiness,
 * workers, or worker health.
 *
 * Responsibility split
 * --------------------
 *  - `ManagedWorker`  — worker-thread mechanics.
 *  - `LifecycleCoordinator` — service-epoch transition serialization.
 *  - `ServiceBase`    — transport registration/startup/rollback/reverse-order
 *                       close, query quiescence, ready state, terminal
 *                       `ReadyChanged(false)` fan-out.
 *  - Services         — domain work loops, dependencies, error/issue policy,
 *                       readiness meaning, and restart/crash policy.
 *
 * The coordinator never decides worker health.  For example, inventory's
 * "restart after crash requires stop() first" rule (Policy A) is evaluated by
 * the service after `beginStart()` reports that the epoch is already running.
 *
 * Locking rule
 * ------------
 * The coordinator mutex is **never** held while service, runtime, worker,
 * transport, or domain code runs.  `beginStart()`/`beginStop()` return once
 * the transition is claimed; the caller then performs the real work outside
 * any coordinator lock and finalizes through the returned guard.
 *
 * Asymmetric abandoned transitions
 * --------------------------------
 * If a transition guard is destroyed without an explicit outcome:
 *  - an abandoned **start** resolves to `stopped`;
 *  - an abandoned **stop** resolves to `stopped`.
 *
 * An abandoned stop is never rolled back to `running`, and the state is never
 * left stuck in `stopping`.  This is deliberate: teardown is structurally
 * non-throwing (`noexcept` query quiescence, guarded self-stop, serialized
 * worker join, exception-isolated final cleanup), so a partially completed
 * teardown must not wedge the lifecycle.
 *
 * Concurrent stop semantics
 * -------------------------
 * A concurrent second `stop()` waits for the active teardown and only returns
 * once `stopped` is visible.  Callback-originated synchronous owner stops
 * (worker, LLDP, netlink, runtime, or exit-handler callbacks calling their own
 * owner's `stop()`) must therefore be rejected or converted into an external
 * shutdown request, or they will wait on themselves.
 *
 * Intentional two-level state
 * ---------------------------
 * `LifecycleCoordinator::State` and `ServiceBase`'s running/readiness state
 * coexist intentionally; see `lib/service_framework/README.md`.
 */
class LifecycleCoordinator {
public:
    enum class State {
        stopped,
        starting,
        running,
        stopping,
    };

    class Transition;

    LifecycleCoordinator() = default;
    ~LifecycleCoordinator() = default;

    LifecycleCoordinator(const LifecycleCoordinator&) = delete;
    LifecycleCoordinator& operator=(const LifecycleCoordinator&) = delete;

    /**
     * @brief Move-only RAII guard for an in-flight transition.
     *
     * `owned()` is false when no transition was claimed (for example
     * `beginStart()` on an already-running service).  Only an owning guard
     * resolves the state on `complete()`/`fail()`/destruction.
     */
    class Transition {
    public:
        Transition() noexcept = default;
        Transition(LifecycleCoordinator* owner, State target) noexcept
            : owner_(owner), target_(target)
        {}

        ~Transition() { abandon(); }

        Transition(Transition&& other) noexcept
            : owner_(other.owner_), target_(other.target_)
        {
            other.owner_ = nullptr;
        }

        Transition& operator=(Transition&& other) noexcept
        {
            if (this != &other) {
                abandon();
                owner_ = other.owner_;
                target_ = other.target_;
                other.owner_ = nullptr;
            }
            return *this;
        }

        Transition(const Transition&) = delete;
        Transition& operator=(const Transition&) = delete;

        /// True when this guard owns an in-flight transition.
        [[nodiscard]] bool owned() const noexcept { return owner_ != nullptr; }
        explicit operator bool() const noexcept { return owned(); }

        /**
         * @brief Resolve successfully: `starting → running`, `stopping → stopped`.
         */
        void complete() noexcept { resolve(true); }

        /**
         * @brief Resolve unsuccessfully.
         *
         * A failed start resolves to `stopped`.  A failed stop also resolves
         * to `stopped` — teardown is never rolled back to `running`.
         */
        void fail() noexcept { resolve(false); }

    private:
        void abandon() noexcept { resolve(false); }
        void resolve(bool success) noexcept;

        LifecycleCoordinator* owner_{nullptr};
        State target_{State::stopped};
    };

    /**
     * @brief Claim a start transition.
     *
     * - `stopped` → claims `starting` and returns an owning transition.
     * - `running` → returns an unowned transition; the caller applies its own
     *   health/restart policy.
     * - `starting`/`stopping` → waits for resolution and re-evaluates.
     */
    [[nodiscard]] Transition beginStart();

    /**
     * @brief Claim a stop transition.
     *
     * - `running` → claims `stopping` and returns an owning transition.
     * - `stopped` → returns an unowned transition.
     * - `starting` → waits for startup resolution, then claims stop if the
     *   startup succeeded.
     * - `stopping` → waits for the active teardown to finish; returns an
     *   unowned transition only once `stopped` is visible.
     */
    [[nodiscard]] Transition beginStop();

    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] bool isStopped() const noexcept;

private:
    void resolveTransition(State target, bool success) noexcept;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    State state_{State::stopped};
};

} // namespace RSCGroup
