#pragma once

#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>

namespace RSCGroup {

/**
 * @brief Serializes complete service-epoch lifecycle transitions.
 *
 * LifecycleCoordinator owns transition serialization only:
 *
 *     stopped -> starting -> running -> stopping -> stopped
 *
 * It coordinates concurrent lifecycle calls and provides RAII completion of
 * in-flight transitions. It knows nothing about transports, readiness,
 * workers, or worker health.
 *
 * Responsibility split
 * --------------------
 *  - ManagedWorker          — worker-thread mechanics.
 *  - LifecycleCoordinator   — lifecycle transition serialization.
 *  - ServiceBase            — transport and readiness management.
 *  - Services/components    — domain work, resources, health and restart
 *                              policy.
 *
 * Locking rule
 * ------------
 * The coordinator mutex is never held while service, worker, transport,
 * cancellation callback, or domain code runs.
 *
 * Cancellable startup
 * -------------------
 * beginCancellableStart() is an opt-in alternative to beginStart(). It gives
 * the startup operation a stop token. A concurrent beginStop() requests that
 * token before waiting for startup rollback to complete.
 *
 * A cancelled start must:
 *
 *  1. stop or roll back all resources created during startup;
 *  2. call CancellableStart::fail();
 *
 * tryComplete() atomically arbitrates between successful startup and a stop
 * request. If cancellation was claimed first, tryComplete() returns false
 * and leaves the lifecycle in starting until rollback calls fail().
 *
 * Callback-originated stop
 * ------------------------
 * The default beginStop() waits for an active transition. Code executing
 * synchronously inside startup must instead call:
 *
 *     beginStop(WaitPolicy::no_wait)
 *
 * Waiting from that thread would deadlock because the same thread must finish
 * startup rollback and resolve the starting transition.
 *
 * isStartOwnerThread() may be used to select the wait policy when an owner
 * requires a synchronous stop() API. It identifies the thread that originally
 * claimed beginCancellableStart(). Cancellable startup execution should remain
 * on that thread if this helper is used.
 *
 * Asymmetric abandoned transitions
 * --------------------------------
 * If a transition guard is destroyed without an explicit outcome:
 *
 *  - an abandoned start resolves to stopped;
 *  - an abandoned stop resolves to stopped.
 *
 * Teardown is never rolled back to running.
 */
class LifecycleCoordinator {
public:
    enum class State {
        stopped,
        starting,
        running,
        stopping,
    };

    /**
     * Controls whether beginStop() waits for an active start/stop transition.
     *
     * wait:
     *   Preserves the original beginStop() semantics. A concurrent caller
     *   returns only after the active transition has resolved.
     *
     * no_wait:
     *   Requests cancellable-start cancellation if applicable, but returns
     *   immediately without waiting for startup rollback or an already-active
     *   stop transition.
     */
    enum class WaitPolicy {
        wait,
        no_wait,
    };

    class Transition;
    class CancellableStart;

    LifecycleCoordinator() = default;
    ~LifecycleCoordinator() = default;

    LifecycleCoordinator(const LifecycleCoordinator&) = delete;
    LifecycleCoordinator& operator=(const LifecycleCoordinator&) = delete;

    /**
     * @brief Move-only RAII guard for an ordinary start or stop transition.
     *
     * owned() is false when no transition was claimed. Only an owning guard
     * changes coordinator state on complete(), fail(), or destruction.
     */
    class Transition {
    public:
        Transition() noexcept = default;

        Transition(
            LifecycleCoordinator* owner,
            State target) noexcept
            : owner_(owner)
            , target_(target)
        {
        }

        ~Transition()
        {
            abandon();
        }

        Transition(Transition&& other) noexcept
            : owner_(other.owner_)
            , target_(other.target_)
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

        [[nodiscard]] bool owned() const noexcept
        {
            return owner_ != nullptr;
        }

        explicit operator bool() const noexcept
        {
            return owned();
        }

        /**
         * Resolve successfully:
         *
         *  - starting -> running
         *  - stopping -> stopped
         */
        void complete() noexcept
        {
            resolve(true);
        }

        /**
         * Resolve unsuccessfully.
         *
         * Both failed startup and failed teardown resolve to stopped.
         */
        void fail() noexcept
        {
            resolve(false);
        }

    private:
        void abandon() noexcept
        {
            resolve(false);
        }

        void resolve(bool success) noexcept;

        LifecycleCoordinator* owner_{nullptr};
        State target_{State::stopped};
    };

    /**
     * @brief Move-only guard for an opt-in cancellable startup transition.
     *
     * A concurrent beginStop() requests the stop token. Successful startup
     * must be published through tryComplete(), not complete().
     */
    class CancellableStart {
    public:
        CancellableStart() noexcept = default;

        CancellableStart(
            LifecycleCoordinator* owner,
            std::stop_token stopToken) noexcept
            : owner_(owner)
            , stopToken_(std::move(stopToken))
        {
        }

        ~CancellableStart()
        {
            fail();
        }

        CancellableStart(CancellableStart&& other) noexcept
            : owner_(other.owner_)
            , stopToken_(std::move(other.stopToken_))
        {
            other.owner_ = nullptr;
        }

        CancellableStart& operator=(
            CancellableStart&& other) noexcept
        {
            if (this != &other) {
                fail();

                owner_ = other.owner_;
                stopToken_ = std::move(other.stopToken_);
                other.owner_ = nullptr;
            }

            return *this;
        }

        CancellableStart(const CancellableStart&) = delete;
        CancellableStart& operator=(
            const CancellableStart&) = delete;

        [[nodiscard]] bool owned() const noexcept
        {
            return owner_ != nullptr;
        }

        explicit operator bool() const noexcept
        {
            return owned();
        }

        [[nodiscard]] std::stop_token stopToken() const noexcept
        {
            return stopToken_;
        }

        [[nodiscard]] bool stopRequested() const noexcept
        {
            return stopToken_.stop_requested();
        }

        /**
         * Atomically attempts to publish starting -> running.
         *
         * @return true if startup won the race and running was published.
         *         false if cancellation had already been requested or this
         *         guard does not own an active cancellable start.
         *
         * On false, an owning guard retains ownership. The caller must finish
         * startup rollback and then call fail().
         */
        [[nodiscard]] bool tryComplete() noexcept;

        /**
         * Resolve starting -> stopped after startup rollback is complete.
         */
        void fail() noexcept;

    private:
        LifecycleCoordinator* owner_{nullptr};
        std::stop_token stopToken_;
    };

    /**
     * Claim an ordinary, non-cancellable start transition.
     *
     * - stopped: claims starting and returns an owning transition.
     * - running: returns an unowned transition.
     * - starting/stopping: waits for resolution and re-evaluates.
     */
    [[nodiscard]] Transition beginStart();

    /**
     * Claim a cancellable start transition.
     *
     * - stopped: claims starting and returns an owning cancellable guard.
     * - running: returns an unowned guard.
     * - starting/stopping: waits for resolution and re-evaluates.
     *
     * A concurrent beginStop() requests the returned stop token.
     */
    [[nodiscard]] CancellableStart beginCancellableStart();

    /**
     * Claim or request a stop transition.
     *
     * running:
     *   Claims stopping and returns an owning transition.
     *
     * stopped:
     *   Returns an unowned transition.
     *
     * cancellable starting:
     *   Atomically publishes cancellation intent, requests the startup stop
     *   token outside the coordinator mutex, and:
     *
     *   - wait: waits for startup rollback, then re-evaluates;
     *   - no_wait: returns immediately with an unowned transition.
     *
     * ordinary starting:
     *   - wait: waits for startup resolution, then re-evaluates;
     *   - no_wait: returns immediately without cancelling startup.
     *
     * stopping:
     *   - wait: waits until stopped;
     *   - no_wait: returns immediately.
     */
    [[nodiscard]] Transition beginStop(
        WaitPolicy waitPolicy = WaitPolicy::wait);

    /**
     * True when called by the thread that originally claimed the active
     * cancellable startup transition.
     *
     * This is intended to help synchronous owner stop() methods choose
     * WaitPolicy::no_wait and avoid waiting for their own startup operation.
     */
    [[nodiscard]] bool isStartOwnerThread() const noexcept;

    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] bool isStopped() const noexcept;

private:
    void resolveTransition(State target, bool success) noexcept;

    [[nodiscard]] bool tryCompleteCancellableStart() noexcept;
    void failCancellableStart() noexcept;

    /**
     * Precondition: mutex_ is held.
     */
    void clearCancellableStartLocked() noexcept;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    State state_{State::stopped};

    bool cancellableStartActive_{false};
    bool startCancellationRequested_{false};
    std::stop_source startStopSource_{std::nostopstate};
    std::thread::id startOwnerThread_;
};

} // namespace RSCGroup
