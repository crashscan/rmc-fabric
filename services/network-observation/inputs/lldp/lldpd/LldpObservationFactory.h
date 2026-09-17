//
// Created by vvass on 18-Sep-26.
//

//
// Created by vvass on 19-Sep-26.
//
#pragma once

#include "ObservationTypes.h"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace RSCGroup {

/// Cached neighbour identity. Defined here so the factory can build both
/// Present and Removed observations from a cache entry without the cache
/// type leaking into every call site.
struct CachedLldpNeighbor {
    std::optional<std::string> rawChassisId;
    std::optional<std::string> rawPortId;
    std::optional<std::string> rawSystemName;
};

/**
 * @brief Single construction point for LldpObservation.
 *
 * Replaces five hand-rolled copies that had already drifted: the keepalive
 * flag existed on only one of them, which is how the keepalive path shipped
 * without suppression in the engine.
 *
 * @param keepalive Periodic re-assertion of already-known state. The engine
 *        refreshes lastSeen and revives Aged/Expired candidates but emits no
 *        model event. Asserted to be Present-only: a keepalive Removed would
 *        be meaningless and the engine does not handle it.
 */
[[nodiscard]] inline LldpObservation makeLldpObservation(
    std::string_view ifname,
    ObservationEvent event,
    const CachedLldpNeighbor& entry,
    bool keepalive = false)
{
    // Enforced rather than documented: the previous contract lived only in a
    // comment on LldpObservation::keepalive.
    assert(!keepalive || event == ObservationEvent::Present);

    LldpObservation obs;
    obs.observedAt       = std::chrono::steady_clock::now();
    obs.kind             = ObservationKind::Lldp;
    obs.localIfname      = std::string(ifname);
    obs.event            = event;
    obs.remoteChassisId  = entry.rawChassisId;
    obs.remotePortId     = entry.rawPortId;
    obs.remoteSystemName = entry.rawSystemName;
    obs.keepalive        = keepalive;
    return obs;
}

/// Overload for producers holding loose fields rather than a cache entry
/// (dispatchChange, the test seam).
[[nodiscard]] inline LldpObservation makeLldpObservation(
    std::string_view ifname,
    ObservationEvent event,
    std::optional<std::string> chassisId,
    std::optional<std::string> portId,
    std::optional<std::string> systemName)
{
    return makeLldpObservation(
        ifname, event,
        CachedLldpNeighbor{std::move(chassisId), std::move(portId), std::move(systemName)});
}

} // namespace RSCGroup
