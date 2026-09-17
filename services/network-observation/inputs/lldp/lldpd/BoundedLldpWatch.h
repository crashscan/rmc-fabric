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
 */
class BoundedLldpWatch {
public:
    using ChangeCallback = std::function<void(std::string_view ifname,
                                              lldpctl_change_t change,
                                              const lldpcli::LldpAtom &interface,
                                              const lldpcli::LldpAtom &neighbor)>;

    BoundedLldpWatch(std::string_view ctlname,
                     std::chrono::milliseconds connectTimeout,
                     std::chrono::milliseconds ioTimeout,
                     ChangeCallback callback);

    ~BoundedLldpWatch();

    BoundedLldpWatch(const BoundedLldpWatch &) = delete;

    BoundedLldpWatch &operator=(const BoundedLldpWatch &) = delete;

    BoundedLldpWatch(BoundedLldpWatch &&) = delete;

    BoundedLldpWatch &operator=(BoundedLldpWatch &&) = delete;

private:
    /// Matches lldpctl_change_callback2 — note there is NO leading
    /// lldpctl_conn_t* (that is the older lldpctl_change_callback).
    static void trampoline(lldpctl_change_t change,
                           lldpctl_atom_t *interface,
                           lldpctl_atom_t *neighbor,
                           void *userData);

    ChangeCallback callback_;
    // Declared before thread_: the loop and the trampoline both use it.
    std::unique_ptr<BoundedLldpConnection> conn_;
    std::jthread thread_;
};
} // namespace RSCGroup
