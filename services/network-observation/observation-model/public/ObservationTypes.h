//
// Created by vvass on 20-Jul-26.
//
/**
 * @file ObservationTypes.h
 * @brief Source-agnostic observation types for the network observation model.
 */
#pragma once
#include <chrono>
#include <optional>
#include <string>

namespace RSCGroup {

enum class NeighborReachability {
    Unknown, Permanent, NoArp, Reachable, Stale, Probe, Delay, Incomplete, Failed
};

enum class FdbEntryKind {
    Unknown, Dynamic, Static, Local, ControlPlane
};

enum class ObservationEvent { Present, Removed };
enum class ObservationKind { Link, Address, Neighbor, Fdb, Lldp };

struct ObservationBase {
    ObservationKind kind;
    std::chrono::steady_clock::time_point observedAt;
};

struct LinkObservation : ObservationBase {
    int ifindex = 0;
    std::string ifname;
    std::string mac;
    bool adminUp = false;
    bool running = false;
    std::string operstate;
    std::optional<std::string> masterIfname;
    /// True if link is present (RTM_NEWLINK), false if removed (RTM_DELLINK)
    ObservationEvent event = ObservationEvent::Present;
};

struct AddressObservation : ObservationBase {
    std::string ifname;
    ObservationEvent event = ObservationEvent::Present;
    int family = 0;
    std::string cidr;
};

struct NeighborObservation : ObservationBase {
    std::string ifname;
    ObservationEvent event = ObservationEvent::Present;
    int family = 0;
    std::string mac;
    std::string ip;
    NeighborReachability reachability = NeighborReachability::Unknown;
};

struct FdbObservation : ObservationBase {
    std::string portIfname;
    ObservationEvent event = ObservationEvent::Present;
    std::string mac;
    FdbEntryKind entryKind = FdbEntryKind::Unknown;
};

struct LldpObservation : ObservationBase {
    std::string localIfname;
    /// True if neighbor is present (added/updated), false if removed
    ObservationEvent event = ObservationEvent::Present;
    std::optional<std::string> remoteChassisId;
    std::optional<std::string> remotePortId;
    std::optional<std::string> remoteSystemName;
};

} // namespace RSCGroup