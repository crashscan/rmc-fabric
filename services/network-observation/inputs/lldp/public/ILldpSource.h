//
// Created by vvass on 24-Jul-26.
//
/**
 * @file ILldpSource.h
 * @brief Backend abstraction for LLDP observation sources.
 *
 * Each backend (lldpd daemon, raw socket, synthetic) implements this
 * interface. The LldpObserver owns one source and bridges its
 * observations to the observation model.
 *
 */
#pragma once
#include <chrono>
#include <string>

namespace RSCGroup {
class ILldpSource {
public:
    virtual ~ILldpSource() = default;

    virtual bool start() = 0;

    /**
     * @brief Stop the source and drain all in-flight callbacks.
     *
     * Postcondition when stop() returns:
     *  - no admitted callback is executing;
     *  - no new callback will be admitted until a successful restart;
     *  - watch/subscription handle is released;
     *  - cached neighbor state is cleared;
     *  - destruction is safe.
     *
     * Implementations that bound these waits may fail to meet them. A
     * backend that does so must log it and refuse to restart rather than
     * report success — see LldpdSource, which latches itself unusable.
     *
     * Must be idempotent: calling stop() on an already-stopped source is
     * a no-op.
     */
    virtual void stop() = 0;

    [[nodiscard]] virtual bool isRunning() const = 0;

    /**
     * @brief Whether the backend's push subscription is still live.
     *
     * Distinct from both neighbours: isRunning() reports the lifecycle
     * epoch, isBackendAlive() actively probes. This is a cheap, passive
     * read of a subscription that may have died without the epoch ending —
     * for lldpd, a watch loop thread that exited when the daemon restarted.
     *
     * The runtime uses it to choose repair over replacement: a dead watch
     * is reconnected in place, preserving the cache that reconciliation
     * diffs against.
     *
     * Backends with no persistent subscription should return isRunning().
     */
    [[nodiscard]] virtual bool isWatchAlive() const = 0;

    /**
     * @brief Trigger a full resync from the source.
     *
     * For push-based backends (e.g. lldpd), this performs an advisory
     * reconnect: a replacement watch is built, the old one retired, and
     * the pre-reconnect cache diffed against what re-enumeration observes
     * so neighbours that genuinely went away are reported Removed. The
     * cache is exchanged and reconciled, NOT cleared.
     *
     * For snapshot or packet-based backends, this triggers an active
     * resync of all interfaces.
     *
     * @return false if the resync did not happen. For lldpd this means the
     *         replacement watch could not be built, or the source was
     *         stopped concurrently — in both cases the existing watch is
     *         retained and the source keeps running. Callers repairing a
     *         known divergence MUST treat false as "still diverged": no
     *         other path will retry on their behalf.
     */
    [[nodiscard]] virtual bool refreshAll() = 0;

    /**
     * @brief Trigger a targeted refresh for one local interface.
     *
     * For push-based backends (e.g. lldpd), this is a no-op — the
     * daemon already pushes per-interface changes continuously.
     *
     * For snapshot or packet-based backends, this triggers an active
     * resync scoped to a single interface.
     */
    virtual void refreshInterface(const std::string &ifname) = 0;

    /**
     * @brief Flush all LLDP state for a removed or downed interface.
     *
     * Emits a Removed observation for every cached neighbor on the
     * given interface, then clears the local cache for that interface.
     *
     * This represents a local topology loss (link down, interface
     * removed) and is NOT gated by the emitRemovals config flag —
     * forced flush always emits removals for correctness.
     *
     * Called by LldpObserver when netlink reports link-down or
     * interface removal.
     */
    virtual void removeInterface(const std::string &ifname) = 0;

    /**
     * @brief Re-emit every currently cached neighbor as a Present
     *        observation with keepalive=true.
     *
     * For push-based backends (lldpd) that do not re-notify unchanged
     * neighbors, this keeps downstream lastSeen fresh so stable neighbors
     * are not aged out. Emits nothing for neighbors the backend reported
     * deleted. No-op when the source is stopped (admission closed).
     *
     * Does NOT update lastEventAt() — keepalives are not backend contact.
     */
    virtual void reassertAll() = 0;

    /**
     * @brief Cheap backend connectivity probe (e.g. an lldpctl round-trip).
     *
     * Used by the runtime liveness watchdog. Watch silence is NOT a valid
     * liveness signal — lldpd only notifies on changes, so a stable network
     * is legitimately silent.
     */
    [[nodiscard]] virtual bool isBackendAlive() const = 0;

    /**
     * @brief Time of the last backend-originated event (watch callback or
     *        enumeration). Informational (status reporting); keepalive
     *        re-assertions and forced flushes never update it.
     *        time_point::min() when no backend event has been observed.
     */
    [[nodiscard]] virtual std::chrono::steady_clock::time_point lastEventAt() const = 0;
};
} // namespace RSCGroup