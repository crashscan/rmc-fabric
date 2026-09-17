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
     * Must be idempotent: calling stop() on an already-stopped source is
     * a no-op.
     */
    virtual void stop() = 0;
    [[nodiscard]] virtual bool isRunning() const = 0;

    /**
     * @brief Trigger a full resync from the source.
     *
     * For push-based backends (e.g. lldpd), this performs an advisory
     * reconnect — the existing watch is torn down, the local neighbor
     * cache is cleared, and a new watch is created. The daemon will
     * re-emit current neighbors as added events on reconnect.
     *
     * For snapshot or packet-based backends, this triggers an active
     * resync of all interfaces.
     */
    virtual void refreshAll() = 0;

    /**
     * @brief Trigger a targeted refresh for one local interface.
     *
     * For push-based backends (e.g. lldpd), this is a no-op — the
     * daemon already pushes per-interface changes continuously.
     *
     * For snapshot or packet-based backends, this triggers an active
     * resync scoped to a single interface.
     */
    virtual void refreshInterface(const std::string& ifname) = 0;

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
    virtual void removeInterface(const std::string& ifname) = 0;

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
    [[nodiscard]] virtual bool isBackendAlive() = 0;

    /**
     * @brief Time of the last backend-originated event (watch callback or
     *        enumeration). Informational (status reporting); keepalive
     *        re-assertions and forced flushes never update it.
     *        time_point::min() when no backend event has been observed.
     */
    [[nodiscard]] virtual std::chrono::steady_clock::time_point lastEventAt() const = 0;

};

} // namespace RSCGroup