//
// Created by vvass on 18-Sep-26.
//
/**
 * @file LldpNeighborCache.h
 * @brief Per-interface LLDP neighbour cache.
 *
 * Owns the cache map, its mutex, and its generation counter. Knows nothing
 * about admission, delivery, lldpd, or observations beyond the identity
 * keying rule — mirrors NetlinkState, which plays the same role for the
 * netlink input.
 *
 * Every mutator bumps the generation while holding the lock. Readers use
 * snapshot(), which returns the generation observed at snapshot time so a
 * caller delivering outside the lock can detect that the cache moved.
 */
#pragma once

#include "LldpObserverTypes.h"
#include "CachedLldpNeighbor.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace RSCGroup {

/// ifname -> resolved identity -> entry.
using NeighborCacheMap = std::unordered_map<std::string, std::unordered_map<std::string, CachedLldpNeighbor> >;

class LldpNeighborCache {
public:
    LldpNeighborCache() = default;

    LldpNeighborCache(const LldpNeighborCache &) = delete;
    LldpNeighborCache &operator=(const LldpNeighborCache &) = delete;

    /**
     * @brief Insert, update, or erase one neighbour.
     *
     * Applies the identity rule itself: a non-MAC identity resolves empty
     * and is NOT cached. The caller still delivers the observation — the
     * v1 limitation is that such neighbours are invisible to
     * interface-flush, not that they are dropped.
     *
     * @return false when the identity was not cacheable.
     */
    bool apply(const LldpObservation &obs);

    /**
     * @brief Remove every neighbour on @p ifname and return them.
     *
     * The caller emits Removed for each. Returning the entries rather than
     * emitting here keeps delivery — and its admission lease — outside the
     * cache lock.
     */
    [[nodiscard]] std::vector<CachedLldpNeighbor> flushInterface(const std::string &ifname);

    /// Replace the contents, returning the previous map. Used by the
    /// reconnect path, which needs the old contents to diff against.
    [[nodiscard]] NeighborCacheMap exchange(NeighborCacheMap replacement);

    void clear();

    /**
     * @brief Copy the map together with the generation observed atomically
     *        with it.
     *
     * Returning both is the point: a caller that delivers outside the lock
     * compares generation() against the returned value to detect that the
     * cache moved mid-delivery.
     */
    [[nodiscard]] std::pair<NeighborCacheMap, std::uint64_t> snapshot() const;

    [[nodiscard]] std::uint64_t generation() const;

private:
    mutable std::mutex mutex_;
    NeighborCacheMap byInterface_;
    std::uint64_t generation_{0};
};

/**
 * @brief Neighbours present in @p oldCache but absent from @p fresh.
 *
 * Free function on purpose: the reconcile diff is pure, so it needs no
 * source, no admission gate, and no daemon to test. This is what replaces
 * LldpdSource::reconcileAfterRefreshForTest().
 */
[[nodiscard]] std::vector<std::pair<std::string, CachedLldpNeighbor> >
diffRemovedNeighbors(const NeighborCacheMap &oldCache, const NeighborCacheMap &fresh);

} // namespace RSCGroup
