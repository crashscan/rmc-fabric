#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

namespace RSCGroup {

/**
 * @brief Owns worker-thread mechanics for a service-owned background loop.
 *
 * ManagedWorker owns mechanics only: worker launch, cooperative stop request,
 * blocking-operation wakeup, serialized join, structured exit capture, and
 * generation-aware restart/reap behavior.
 *
 * It has no knowledge of service epochs, readiness, transports, issue codes,
 * or restart policy. Those policies remain with the owning component.
 *
 * Generation semantics
 * --------------------
 * Each successful start() creates a distinct worker generation represented by
 * an internal shared control object.
 *
 * stop() and join() operate on the generation visible when the call begins:
 *
 * - If generation N is visible, the operation never affects replacement
 *   generation N+1.
 * - If no generation is visible because a concurrent start() already owns
 *   operation serialization but has not published its generation, stop() or
 *   join() adopts that concurrently launched generation after acquiring the
 *   operation mutex.
 *
 * A generation is not retired until:
 *
 * - its worker thread has been joined; and
 * - any wake callback already running for that generation has completed.
 *
 * This ensures that a delayed stop or wake request for generation N cannot
 * affect worker or wake resources belonging to generation N+1.
 *
 * Threading and lock model
 * ------------------------
 * start(), stop(), join(), and isJoinable() serialize ownership operations on
 * an internal operation mutex.
 *
 * requestStop() is safe from any thread, including the worker thread. It
 * requests cooperative cancellation and may invoke Wake, but never joins.
 *
 * The effective internal lock order is:
 *
 *     operation mutex -> generation wake mutex -> state mutex
 *
 * Not every path acquires every mutex. In particular:
 *
 * - state mutex is never held while requesting stop, joining, or invoking
 *   owner callbacks;
 * - request_stop() is never called while any ManagedWorker mutex is held;
 * - the worker entry point, epilogue, and ExitHandler never acquire the
 *   operation mutex;
 * - joining never occurs while the state mutex or wake mutex is held.
 *
 * request_stop() executes registered stop callbacks synchronously. Calling it
 * outside all ManagedWorker mutexes is therefore a required invariant.
 *
 * stop() and join() called from the worker thread are rejected with
 * std::logic_error. There is no detach path.
 *
 * Work contract
 * -------------
 * Work:
 *
 * - receives the current generation's std::stop_token;
 * - must tolerate receiving an already-stopped token;
 * - must return promptly after stop is requested;
 * - may throw; exceptions are captured into Exit;
 * - may not execute at all if ManagedWorker observes a stop request before
 *   invoking Work.
 *
 * A stop request can race the final pre-invocation token check. Work must
 * therefore still inspect and honor its token even though ManagedWorker checks
 * it before invocation.
 *
 * If Work is skipped because stop was already requested, the generation
 * finalizes with ExitReason::stop_requested.
 *
 * Wake contract
 * -------------
 * Wake:
 *
 * - must be non-blocking and non-throwing;
 * - may run even when Work never begins, because stopping a published
 *   generation still performs its normal wake operation;
 * - is delivered at most once per generation;
 * - when delivered, runs synchronously on the thread whose request first
 *   successfully changes that generation to stop-requested;
 * - must not call start(), stop(), join(), requestStop(), or isJoinable() on
 *   this ManagedWorker;
 * - may call state-only queries such as isRunning(), isCurrentThread(), and
 *   lastExit().
 *
 * Generation retirement waits for an in-flight Wake invocation. Calling an
 * operation-mutex method from Wake could therefore deadlock.
 *
 * Owner resources used by Wake must be initialized before start() is called
 * and must remain valid until the generation has been joined and retired.
 *
 * ExitHandler contract
 * --------------------
 * ExitHandler:
 *
 * - runs on the worker thread after running state has been cleared and
 *   lastExit() has been recorded;
 * - runs before the worker thread identity is cleared;
 * - must be non-blocking and non-throwing;
 * - must not call start(), stop(), join(), or isJoinable() on this worker;
 * - may call isRunning(), isCurrentThread(), lastExit(), and requestStop().
 *
 * An external join may hold the operation mutex while waiting for ExitHandler
 * to complete. ExitHandler must therefore never acquire the operation mutex.
 *
 * std::function cannot express noexcept. Exceptions from Wake and ExitHandler
 * are caught and logged.
 *
 * Wake failure policy
 * -------------------
 * ManagedWorker invokes Wake but does not decide the owning component's policy
 * for an underlying signaling failure.
 *
 * A component with another guaranteed wake path or finite timeout may log and
 * tolerate delayed shutdown. A component whose worker may block indefinitely
 * with Wake as its only deterministic interrupt path may instead use an
 * abort-over-hang policy.
 *
 * Member destruction order
 * ------------------------
 * If Work, Wake, or ExitHandler captures the owning object, declare
 * ManagedWorker after every sibling member those callbacks access. Reverse
 * member destruction then stops and joins the worker before those resources
 * are destroyed.
 *
 * Exit reason semantics
 * ---------------------
 * ExitReason::returned versus ExitReason::stop_requested is advisory.
 *
 * The reason reflects the generation token's state when the worker finalizes,
 * not necessarily whether the stop request caused Work to return. A stop that
 * arrives after Work returns but before final classification may therefore
 * produce ExitReason::stop_requested.
 *
 * Object lifetime
 * ---------------
 * ManagedWorker serializes concurrent member calls, but it does not make
 * concurrent destruction safe. The owner must remain alive for the complete
 * duration of every concurrent call.
 */
class ManagedWorker {
public:
    using Work = std::function<void(std::stop_token)>;
    using Wake = std::function<void()>;

    enum class ExitReason {
        not_started,
        stop_requested,
        returned,
        exception,
    };

    struct Exit {
        ExitReason reason{ExitReason::not_started};
        std::exception_ptr exception;
    };

    using ExitHandler = std::function<void(const Exit&)>;

    /**
     * @param name Human-readable worker name used in diagnostics.
     * @param work Worker body.
     * @param wake Optional blocking-operation wake callback.
     * @param onExit Optional worker-exit callback.
     *
     * @throws std::invalid_argument if work is empty.
     */
    ManagedWorker(
        std::string name,
        Work work,
        Wake wake = {},
        ExitHandler onExit = {});

    /**
     * Requests stop and joins the current generation.
     *
     * Destruction from the worker thread is an ownership violation and is
     * reported fatally. ManagedWorker never detaches its worker.
     */
    ~ManagedWorker();

    ManagedWorker(const ManagedWorker&) = delete;
    ManagedWorker& operator=(const ManagedWorker&) = delete;

    /**
     * Launches a new worker generation.
     *
     * If a previous generation finished but remains unjoined, it is joined and
     * retired before the new generation is created.
     *
     * @return true if a generation was launched.
     * @return false if Work is currently executing.
     *
     * @throws Exceptions from std::jthread construction.
     */
    [[nodiscard]] bool start();

    /**
     * Requests cooperative stop for the generation current at invocation.
     *
     * Safe from any thread, including the worker thread. Does not join.
     */
    void requestStop() noexcept;

    /**
     * Joins and retires the generation observed at call entry.
     *
     * If that generation is retired and replaced while this call waits for
     * operation serialization, the replacement is not joined.
     *
     * If no generation is initially visible because a concurrent start()
     * already owns operation serialization, this call adopts and joins that
     * concurrently launched generation.
     *
     * @throws std::logic_error if called from the worker thread.
     */
    void join();

    /**
     * Requests stop, wakes, joins, and retires the generation observed at call
     * entry.
     *
     * A replacement generation is never affected by a stale stop call.
     *
     * If no generation is initially visible because a concurrent start()
     * already owns operation serialization, this call adopts and stops that
     * concurrently launched generation.
     *
     * @throws std::logic_error if called from the worker thread.
     */
    void stop();

    /**
     * Returns true while the current generation's Work is executing or is
     * about to execute.
     *
     * This becomes false before ExitHandler is invoked.
     */
    [[nodiscard]] bool isRunning() const noexcept;

    /**
     * Returns whether ManagedWorker owns a joinable worker thread.
     *
     * Must not be called from Work, Wake, or ExitHandler.
     */
    [[nodiscard]] bool isJoinable() const noexcept;

    /**
     * Returns true when called from the current generation's worker thread.
     *
     * Remains true for the complete ExitHandler invocation.
     */
    [[nodiscard]] bool isCurrentThread() const noexcept;

    /**
     * Returns the most recently recorded exit.
     *
     * A new generation resets this to ExitReason::not_started.
     */
    [[nodiscard]] Exit lastExit() const;

    [[nodiscard]] const std::string& workerName() const noexcept
    {
        return name_;
    }

private:
    /**
     * Per-generation stop and wake control.
     *
     * Shared ownership allows a delayed caller to retain the generation it
     * observed even after that generation is no longer current. Shared-pointer
     * identity is the generation identity.
     */
    struct GenerationControl {
        /**
         * Fresh, valid, non-requested stop state for this generation.
         *
         * It is created with the generation and is never replaced. Work
         * receives this source's token rather than std::jthread's internal
         * token.
         */
        std::stop_source stopSource;

        /**
         * Serializes Wake invocation with generation retirement.
         */
        std::mutex wakeMutex;
        bool wakeEnabled{true};
    };

    using GenerationPtr = std::shared_ptr<GenerationControl>;

    [[nodiscard]] GenerationPtr
    currentGeneration() const noexcept;

    [[nodiscard]] bool
    isCurrentGeneration(
        const GenerationPtr& generation) const noexcept;

    void requestStopForGeneration(
        const GenerationPtr& generation) noexcept;

    /**
     * Joins and retires generation.
     *
     * Precondition: opMutex_ is held.
     */
    void joinAndRetireLocked(
        const GenerationPtr& generation);

    /**
     * Disables generation Wake and clears current-generation state.
     *
     * May wait for a Wake invocation already in progress.
     */
    void retireGeneration(
        const GenerationPtr& generation) noexcept;

    void invokeWake() noexcept;

    std::string name_;
    Work work_;
    Wake wake_;
    ExitHandler onExit_;

    /**
     * Serializes ownership operations on thread_.
     */
    mutable std::mutex opMutex_;

    /**
     * Guards running_, lastExit_, workerThreadId_, and currentGeneration_.
     */
    mutable std::mutex stateMutex_;

    bool running_{false};
    Exit lastExit_{};
    std::thread::id workerThreadId_{};
    GenerationPtr currentGeneration_;

    /**
     * Declared last so it is destroyed before callback-accessed owner state.
     */
    std::jthread thread_;
};

} // namespace RSCGroup
