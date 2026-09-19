//
// Created by vvass on 17-Sep-26.
//
#pragma once

#include "BoundedLldpConnection.h"

#include <lldpctl.h>
#include <lldpctl.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>

namespace RSCGroup {
/**
 * @brief Watch over a BoundedLldpConnection.
 *
 * Replaces lldpcli::LldpWatch, which creates its own connection from
 * liblldpctl's default (unbounded) transport with no injection point — the
 * subscribe round-trip in its constructor hangs indefinitely against a wedged
 * lldpd, and that constructor runs on the supervision thread via
 * refreshAll(), so it could block ObservationService::stop().
 *
 * Bounding strategy:
 *  - Construction (connect + lldpctl_watch_callback2 subscribe) is bounded by
 *    the connection's connect/io timeouts. This is the hang that mattered.
 *  - The watch loop then blocks with NO deadline — an idle link is legitimately
 *    silent — but interruptibly: stop() sets the stop token, calls
 *    lldpctl_watch_sync_unblock(), and signals the connection's wakeup fd, so
 *    the loop unwinds promptly. Destruction is therefore bounded by one
 *    in-flight callback, not by lldpd's liveness.
 *
 * @section deferred Deferred callback attachment
 * The callback is separable from the watch's lifetime. A watch constructed
 * without one subscribes to lldpd and drains its socket, but DISCARDS every
 * event until setCallback() attaches a handler.
 *
 * This exists for the reconnect in LldpdSource::refreshAll(): the new watch
 * is subscribed while the old one is still delivering, so both are connected
 * and no event is missed at the daemon, but only one of them is ever
 * dispatching. The owner destroys the old watch — which joins its loop thread
 * and is therefore a hard barrier, not a race — before attaching the callback
 * here. Events dropped in that window are recovered by the owner's
 * re-enumeration pass.
 */
class BoundedLldpWatch {
public:
    using ChangeCallback = std::function<void(std::string_view ifname,
                                              lldpctl_change_t change,
                                              const lldpcli::LldpAtom &interface,
                                              const lldpcli::LldpAtom &neighbor)>;

    /**
     * @brief Invoked once when the watch loop terminates on its own.
     *
     * Runs ON the loop thread as its final act, so it must not block and must
     * not destroy this watch — that would join the thread from itself.
     * Post a request; do not act inline.
     *
     * NOT invoked during destruction: a caller destroying the watch already
     * knows it is gone. This fires only for the unsolicited case — lldpd
     * died, the socket closed, lldpctl_watch() returned an error.
     */
    using ExitHandler = std::function<void()>;

    /// Subscribes immediately; discards events until setCallback().
    BoundedLldpWatch(std::string_view ctlname,
                     std::chrono::milliseconds connectTimeout,
                     std::chrono::milliseconds ioTimeout);

    BoundedLldpWatch(std::string_view ctlname,
                     std::chrono::milliseconds connectTimeout,
                     std::chrono::milliseconds ioTimeout,
                     ChangeCallback callback);

    ~BoundedLldpWatch();

    BoundedLldpWatch(const BoundedLldpWatch &) = delete;
    BoundedLldpWatch &operator=(const BoundedLldpWatch &) = delete;
    BoundedLldpWatch(BoundedLldpWatch &&) = delete;
    BoundedLldpWatch &operator=(BoundedLldpWatch &&) = delete;

    /**
     * @brief Attach or replace the change handler.
     *
     * Safe to call while the loop thread is running. A callback already
     * executing runs to completion with the previous handler; the next
     * dispatch uses the new one.
     *
     * Passing an empty callback resumes discarding.
     */
    void setCallback(ChangeCallback callback);

    /// Must be set before the loop can exit meaningfully; safe concurrently.
    void setExitHandler(ExitHandler onExit);

    /// True once the loop thread has left its dispatch loop.
    [[nodiscard]] bool hasExited() const noexcept { return exited_.load(); }
private:
    /// Matches lldpctl_change_callback2 — note there is NO leading
    /// lldpctl_conn_t* (that is the older lldpctl_change_callback).
    static void trampoline(lldpctl_change_t change,
                           lldpctl_atom_t *interface,
                           lldpctl_atom_t *neighbor,
                           void *userData);

    void notifyExit() noexcept;

    mutable std::mutex exitMutex_;
    ExitHandler onExit_;
    std::atomic<bool> exited_{false};

    /// Guards callback_ against concurrent setCallback() and dispatch.
    /// A mutex, not an atomic<shared_ptr>: dispatch is low-frequency and
    /// the lock is never held across anything but the copy.
    mutable std::mutex callbackMutex_;
    ChangeCallback callback_;

    // Declared before thread_: the loop and the trampoline both use it.
    std::unique_ptr<BoundedLldpConnection> conn_;
    std::jthread thread_;
};
} // namespace RSCGroup
