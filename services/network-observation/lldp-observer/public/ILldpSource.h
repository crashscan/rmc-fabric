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
#include <string>

namespace RSCGroup {

class ILldpSource {
public:
    virtual ~ILldpSource() = default;

    virtual bool start() = 0;
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
};

} // namespace RSCGroup