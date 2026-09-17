//
// Created by vvass on 18-Sep-26.
//

#include "NetlinkObservationMapper.h"

#include <linux/rtnetlink.h>

#include <utility>

namespace RSCGroup {

NeighborReachability nudToReachability(unsigned short nudState) noexcept {
    using enum NeighborReachability;
    switch (nudState) {
        case NUD_INCOMPLETE: return Incomplete;
        case NUD_REACHABLE:  return Reachable;
        case NUD_STALE:      return Stale;
        case NUD_DELAY:      return Delay;
        case NUD_PROBE:      return Probe;
        case NUD_FAILED:     return Failed;
        case NUD_NOARP:      return NoArp;
        case NUD_PERMANENT:  return Permanent;
        default:             return Unknown;
    }
}

FdbEntryKind fdbToEntryKind(const FdbEvent& e) noexcept {
    if (e.local)     return FdbEntryKind::Local;
    if (e.permanent) return FdbEntryKind::Static;
    return FdbEntryKind::Dynamic;
}

ObservationEvent toObsEvent(bool present) noexcept {
    return present ? ObservationEvent::Present : ObservationEvent::Removed;
}

std::string makeCidr(const InterfaceIpEvent& e) {
    return e.address + "/" + std::to_string(static_cast<int>(e.prefixLen));
}

LinkObservation toLinkObservation(const LinkEvent& e,
                                  std::chrono::steady_clock::time_point observedAt) {
    LinkObservation obs;
    obs.observedAt    = observedAt;
    obs.kind          = ObservationKind::Link;
    obs.ifindex       = e.ifindex;
    obs.ifname        = e.ifname;
    obs.mac           = e.mac;
    obs.adminUp       = e.adminUp;
    obs.running       = e.running;
    obs.operstate     = std::to_string(e.operState);
    obs.masterIfname  = e.masterIfname;
    obs.event         = toObsEvent(e.present);
    return obs;
}

AddressObservation toAddressObservation(const InterfaceIpEvent& e,
                                        std::chrono::steady_clock::time_point observedAt) {
    AddressObservation obs;
    obs.observedAt = observedAt;
    obs.kind       = ObservationKind::Address;
    obs.ifname     = e.ifname;
    obs.event      = toObsEvent(e.present);
    obs.family     = e.family;
    obs.cidr       = makeCidr(e);
    return obs;
}

NeighborObservation toNeighborObservation(const NeighborEvent& e,
                                          std::chrono::steady_clock::time_point observedAt) {
    NeighborObservation obs;
    obs.observedAt    = observedAt;
    obs.kind          = ObservationKind::Neighbor;
    obs.ifname        = e.ifname;
    obs.event         = toObsEvent(e.present);
    obs.family        = e.family;
    obs.mac           = e.mac;
    obs.ip            = e.ip;
    obs.reachability  = nudToReachability(e.nudState);
    return obs;
}

FdbObservation toFdbObservation(const FdbEvent& e,
                                std::chrono::steady_clock::time_point observedAt) {
    FdbObservation obs;
    obs.observedAt  = observedAt;
    obs.kind        = ObservationKind::Fdb;
    obs.portIfname  = e.ifname;
    obs.event       = toObsEvent(e.present);
    obs.mac         = e.mac;
    obs.entryKind   = fdbToEntryKind(e);
    return obs;
}

MonitorCallbacks makeCallbacks(ObservationSinks sinks) {
    MonitorCallbacks cb;
    const auto now = [] { return std::chrono::steady_clock::now(); };

    cb.onLinkChanged = [sinks, now](const LinkEvent& e) {
        if (sinks.onLink) {
            sinks.onLink(toLinkObservation(e, now()));
        }
        if (sinks.onLinkStateForLldp) {
            // Resolved per event: the observer may have been created after
            // monitor start by a tick()-driven retry.
            sinks.onLinkStateForLldp(e.ifname, e.present && e.running);
        }
    };

    cb.onInterfaceIpChanged = [sinks, now](const InterfaceIpEvent& e) {
        if (sinks.onAddress) sinks.onAddress(toAddressObservation(e, now()));
    };

    cb.onFdbChanged = [sinks, now](const FdbEvent& e) {
        if (sinks.onFdb) sinks.onFdb(toFdbObservation(e, now()));
    };

    cb.onNeighborChanged = [sinks, now](const NeighborEvent& e) {
        if (sinks.onNeighbor) sinks.onNeighbor(toNeighborObservation(e, now()));
    };

    return cb;
}

} // namespace RSCGroup
