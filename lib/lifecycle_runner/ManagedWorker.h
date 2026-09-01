#pragma once

#include <exception>
#include <functional>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

namespace RSCGroup {

/**
 * @brief Owns worker-thread mechanics for a service-owned background loop.
 *
 * `ManagedWorker` owns **mechanics only**: launch, cooperative stop request,
 * wake, serialized join, exit capture, and restart/reap behavior.  It has no
 * knowledge of service epochs, readiness, transports, issue codes, or restart
 * policy.  Those remain owned by the service (see `LifecycleCoordinator` for
 * service-epoch serialization and the services themselves for health policy).
 *
 * Threading model
 * ---------------
 * `start()`, `stop()` and `join()` are serialized on an internal operation
 * mutex, so two external threads may call `stop()`/`join()` concurrently
 * without double-joining the underlying `std::jthread`.  `requestStop()` is
 * safe from any thread, including the worker thread itself, and never joins.
 *
 * `join()` and `stop()` called from the worker thread are rejected with a
 * deterministic `std::logic_error`.  **There is no `detach()` path**: a
 * detached thread that captures the owning object creates a use-after-free
 * window and breaks producer-drain guarantees.
 *
 * Callback contracts
 * ------------------
 *  - `Work` receives a `std::stop_token` and must return promptly once
 *    `stop_requested()` becomes true.  Exceptions are captured into `Exit`
 *    and never escape the thread entry point.
 *  - `Wake` must be non-blocking and must not throw.  It is invoked exactly
 *    once per real stop request issued through `requestStop()`/`stop()` while
 *    the worker is live, to interrupt a blocking poll/CV wait.
 *  - `ExitHandler` must not throw, runs **on the worker thread** after
 *    `running` has been cleared and `lastExit()` has been recorded, and must
 *    not synchronously drive its own worker's lifecycle: it must not call
 *    `start()`, `stop()` or `join()` on the owning `ManagedWorker`.
 *
 * `std::function` cannot express `noexcept`, so violations of the
 * non-throwing contracts are caught and logged rather than being allowed to
 * abort the process or wedge worker state.
 *
 * Violation-policy asymmetry
 * --------------------------
 * Wake and exit-handler callbacks are *signaling* mechanisms, so their
 * exceptions are contained; containment is degradation tolerance, not free
 * recovery.  A swallowed or failed wake may delay stop until the worker's
 * natural poll/CV wake interval (up to the inventory reconcile interval or
 * the observation aging interval).  By contrast, query quiescence in
 * `ServiceBase`/`IServiceTransport` is a structural safety barrier and is
 * `noexcept` and local-only.
 *
 * Member destruction-order requirement
 * ------------------------------------
 * > If worker callbacks capture the owning object, declare the
 * > `ManagedWorker` member **after** every sibling member those callbacks
 * > access, so the worker is destroyed first during reverse member
 * > destruction.
 *
 * Advisory exit reason
 * --------------------
 * `ExitReason::returned` versus `ExitReason::stop_requested` is **advisory**:
 * a worker returning on its own may race a concurrent stop request.  Services
 * must not treat the distinction as an authoritative synchronization fact.
 */
class ManagedWorker {
public:
    using Work = std::function<void(std::stop_token)>;
    using Wake = std::function<void()>;

    enum class ExitReason {
        not_started,     ///< Never launched, or reset before first launch.
        stop_requested,  ///< Work returned with a stop request outstanding.
        returned,        ///< Work returned without an outstanding stop request.
        exception,       ///< Work terminated by throwing.
    };

    struct Exit {
        ExitReason reason{ExitReason::not_started};
        std::exception_ptr exception;
    };

    using ExitHandler = std::function<void(const Exit&)>;

    /**
     * @param name    Human-readable worker name used in log messages.
     * @param work    Loop body; must return when `stop_requested()` is true.
     * @param wake    Optional non-blocking, non-throwing wake signal.
     * @param onExit  Optional non-throwing exit notification (worker thread).
     */
    ManagedWorker(std::string name,
                  Work work,
                  Wake wake = {},
                  ExitHandler onExit = {});

    /**
     * @brief Requests stop and joins.  Never detaches.
     *
     * Destruction from the worker thread is an ownership/programming
     * violation.  Joining oneself is impossible and detaching is forbidden,
     * so the violation is reported fatally and the process aborts with an
     * actionable diagnostic rather than terminating inside `~jthread` on a
     * failed self-join.
     */
    ~ManagedWorker();

    ManagedWorker(const ManagedWorker&) = delete;
    ManagedWorker& operator=(const ManagedWorker&) = delete;

    /**
     * @brief Launch the worker thread.
     *
     * Under the operation mutex all three prior states are defined:
     *  - `running == true`             → return false (already running);
     *  - `!running && thread joinable` → reap the finished worker, then launch;
     *  - `!thread joinable`            → launch.
     *
     * The finished-but-unjoined worker is reaped by an explicit `join()`
     * rather than by move-assigning a fresh `std::jthread` over it, because
     * move-assignment implicitly requests stop and joins **without** invoking
     * the configured wake callback.
     *
     * @return true if a new worker was launched; false if already running.
     * @throws Any exception from `std::jthread` construction.  The object is
     *         left restartable.
     */
    [[nodiscard]] bool start();

    /**
     * @brief Request cooperative stop and fire the wake callback.
     *
     * Safe from any thread, including the worker.  Does not join.  A stop
     * request on an already-stopping/finished worker does not re-fire wake.
     */
    void requestStop() noexcept;

    /**
     * @brief Join the worker thread.  Idempotent and internally serialized.
     * @throws std::logic_error if called from the worker thread.
     */
    void join();

    /**
     * @brief `requestStop()` followed by `join()`.  Idempotent.
     * @throws std::logic_error if called from the worker thread.
     */
    void stop();

    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] bool isJoinable() const noexcept;

    /**
     * @brief True when the calling thread is this worker's thread.
     *
     * Remains true for the complete duration of the exit handler, so
     * self-operation detection works from inside the handler.
     */
    [[nodiscard]] bool isCurrentThread() const noexcept;

    [[nodiscard]] Exit lastExit() const;

    [[nodiscard]] const std::string& workerName() const noexcept { return name_; }

private:
    void joinLocked();
    void invokeWake() noexcept;

    std::string name_;
    Work work_;
    Wake wake_;
    ExitHandler onExit_;

    /// Serializes start/stop/join so no two callers can join concurrently.
    mutable std::mutex opMutex_;
    /// Guards running_, lastExit_ and workerThreadId_.
    mutable std::mutex stateMutex_;

    bool running_{false};
    Exit lastExit_{};
    std::thread::id workerThreadId_{};
    /// Copy of the live thread's stop source; lets requestStop() avoid opMutex_.
    std::stop_source stopSource_{std::nostopstate};

    /// Declared last: the thread must be reaped before the state it reads.
    std::jthread thread_;
};

} // namespace RSCGroup
