//
// Created by vvass on 19-Sep-26.
//
#pragma once

#include "BoundedLldpWatch.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace RSCGroup {
/**
 * @brief Owns the lldpd watch connection across reconnects.
 *
 * All watch lifetime — build, the silent-build reconnect sequence, teardown,
 * and dead-watch detection — lives here, so LldpdSource is left owning only
 * admission, cache, and liveness.
 *
 * @section ownership Exclusive ownership
 * The watch is held by unique_ptr and never shared. Sharing it would let a
 * concurrent reconnect outlive stop() and run ~BoundedLldpWatch — and so the
 * thread join that guarantees no callback is executing — on the wrong thread,
 * after stop() has already returned and promised the opposite.
 *
 * @section reconnect Reconnect ordering
 * refresh() builds the replacement SILENT and outside the lock, then under the
 * lock destroys the old watch (joining its loop thread: a hard barrier, not a
 * race), runs the caller's swap hook, and only then attaches the callback.
 * Exactly one watch ever dispatches, so a reconnect produces no duplicate
 * Present and no late Removed from the retired epoch.
 *
 * @section threading Threading
 * Every method is safe to call concurrently. stop() is bounded because a
 * concurrent refresh may hold the lock across a watch teardown that is itself
 * waiting on an in-flight downstream callback.
 */
class LldpWatchSupervisor {
public:
    using CallbackFactory = std::function<BoundedLldpWatch::ChangeCallback()>;

    LldpWatchSupervisor(std::string ctlPath,
                        std::chrono::milliseconds connectTimeout,
                        std::chrono::milliseconds ioTimeout);

    ~LldpWatchSupervisor();

    LldpWatchSupervisor(const LldpWatchSupervisor &) = delete;
    LldpWatchSupervisor &operator=(const LldpWatchSupervisor &) = delete;

    /// @return false if no watch could be built; nothing is installed.
    [[nodiscard]] bool start(const CallbackFactory &makeCallback);

    /**
     * @brief Reconnect.
     *
     * @param onSwap Runs under the lock between teardown and attach. The
     *        caller captures its pre-reconnect cache here so nothing can
     *        repopulate it in the gap — doing it outside would let the new
     *        watch's events land first and silently void the reconciliation.
     *
     * @return false if the build failed (existing watch retained, still
     *         dispatching) or the supervisor was stopped meanwhile.
     */
    [[nodiscard]] bool refresh(const CallbackFactory &makeCallback,
                               const std::function<void()> &onSwap = {});

    /**
     * @brief Tear down.
     *
     * Postcondition on true: no watch callback is executing or can start.
     *
     * @return false if @p timeout elapsed waiting for a concurrent refresh.
     *         The watch is then deliberately leaked — destroying it would
     *         join a thread that is not coming back — and the postcondition
     *         does NOT hold. Callers must treat the supervisor as unusable.
     */
    [[nodiscard]] bool stop(std::chrono::milliseconds timeout);

    /// False once a watch has died unsolicited, or none is installed.
    [[nodiscard]] bool isWatchAlive() const;

private:
    [[nodiscard]] std::unique_ptr<BoundedLldpWatch> buildSilent() const;
    void armExitHandler(BoundedLldpWatch &watch);

    const std::string ctlPath_;
    const std::chrono::milliseconds connectTimeout_;
    const std::chrono::milliseconds ioTimeout_;

    /// timed_mutex so stop() can bound its wait: a refresh may hold this
    /// across a teardown blocked on an in-flight downstream callback.
    mutable std::timed_mutex mutex_;
    std::unique_ptr<BoundedLldpWatch> watch_;
    bool stopped_{true};

    /// Set from the dying loop thread; read without the lock, because the
    /// handler must never contend with a refresh that is joining it.
    std::atomic<bool> watchDied_{false};
};
} // namespace RSCGroup
