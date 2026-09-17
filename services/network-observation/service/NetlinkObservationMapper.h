//
// Created by vvass on 18-Sep-26.
//
//
// Netlink event → observation translation.
//
#pragma once

#include "NetlinkTypes.h"
#include "ObservationTypes.h"

#include <chrono>
#include <functional>
#include <string>

namespace RSCGroup {
/**
 * @brief Sinks supplied to makeCallbacks().
 *
 * Extracted from NetlinkLldpObservationRuntime so the callback wiring can be
 * built and invoked without a runtime, a model, or a live netlink socket.
 *
 * The separation matters for one behaviour in particular: onLinkStateForLldp
 * is resolved per event rather than captured once, so a link event reaches an
 * LLDP observer created AFTER the monitor started. That is the entire purpose
 * of the atomic<shared_ptr<LldpObserver>> indirection, and it was previously
 * unverifiable because makeCallbacks() was a private member.
 */
struct ObservationSinks {
    std::function<void(LinkObservation)> onLink;
    std::function<void(AddressObservation)> onAddress;
    std::function<void(NeighborObservation)> onNeighbor;
    std::function<void(FdbObservation)> onFdb;

    /// Invoked after onLink for every link event. `up` is false when the
    /// interface is absent or not running. May be empty.
    std::function<void(const std::string &ifname, bool up)> onLinkStateForLldp;
};

// --- pure field mappings -------------------------------------------------

[[nodiscard]] NeighborReachability nudToReachability(unsigned short nudState) noexcept;

[[nodiscard]] FdbEntryKind fdbToEntryKind(const FdbEvent &e) noexcept;

[[nodiscard]] ObservationEvent toObsEvent(bool present) noexcept;

[[nodiscard]] std::string makeCidr(const InterfaceIpEvent &e);

// --- event → observation -------------------------------------------------

[[nodiscard]] LinkObservation toLinkObservation(
    const LinkEvent &e, std::chrono::steady_clock::time_point observedAt);

[[nodiscard]] AddressObservation toAddressObservation(
    const InterfaceIpEvent &e, std::chrono::steady_clock::time_point observedAt);

[[nodiscard]] NeighborObservation toNeighborObservation(
    const NeighborEvent &e, std::chrono::steady_clock::time_point observedAt);

[[nodiscard]] FdbObservation toFdbObservation(
    const FdbEvent &e, std::chrono::steady_clock::time_point observedAt);

/**
 * @brief Build MonitorCallbacks that translate events and forward to @p sinks.
 *
 * Callbacks run on the netlink monitor thread. Empty sinks are skipped, so a
 * caller may wire only the events it needs.
 */
[[nodiscard]] MonitorCallbacks makeCallbacks(ObservationSinks sinks);
} // namespace RSCGroup
