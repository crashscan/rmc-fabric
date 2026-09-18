//
// Created by vvass on 18-Sep-26.
//
/**
 * @file ObservationItem.h
 * @brief One queued observation plus the source that produced it.
 */
#pragma once

#include "ObservationTypes.h"

#include <cstdint>
#include <variant>

namespace RSCGroup {

/**
 * @brief Which producer emitted an observation.
 *
 * Carried per item so the queue's drop handler can record WHICH source may
 * now be divergent. A netlink drop needs a redump; an LLDP drop needs a
 * watch refresh. Without the tag a single drop would force both.
 */
enum class ObservationSource : std::uint8_t {
    Netlink = 0,
    Lldp = 1,
};

/// Bit for @p source in a resync mask. Mask, not a set: it is read and
/// cleared atomically from a drop handler that runs under the queue mutex.
[[nodiscard]] constexpr std::uint32_t sourceBit(ObservationSource source) noexcept {
    return 1u << static_cast<std::uint8_t>(source);
}

using ObservationPayload = std::variant<
    LinkObservation,
    AddressObservation,
    NeighborObservation,
    FdbObservation,
    LldpObservation>;

/**
 * @brief A single observation in transit from producer to model.
 *
 * Closed variant rather than a polymorphic base: the set of observation
 * kinds is fixed by INetworkObservationModel, and a variant keeps
 * dispatch exhaustive at compile time — a new kind fails to build until
 * applyObservation() handles it.
 *
 * Mapping from netlink events happens on the producer thread, BEFORE the
 * push, so the queue carries observations rather than raw kernel events.
 * That keeps NetlinkObservationMapper's contract unchanged and keeps the
 * consumer free of netlink types.
 */
struct ObservationItem {
    ObservationSource  source{ObservationSource::Netlink};
    ObservationPayload payload;
};

} // namespace RSCGroup