//
// Created by vvass on 18-Sep-26.
//
#include "LldpNeighborCache.h"

#include "LldpUtils.h"

#include <glog/logging.h>

#include <utility>

namespace RSCGroup {

bool LldpNeighborCache::apply(const LldpObservation &obs) {
    // Identity rule lives here so every writer keys identically. The old
    // reconcileAfterRefreshForTest() had to duplicate this to build a
    // comparable snapshot; with one owner it cannot drift.
    const std::string key = resolveLldpIdentity(obs.remoteChassisId, obs.remotePortId);
    if (key.empty()) {
        // v1 limitation: non-MAC identities are forwarded downstream but not
        // cached, so they are invisible to interface flush and keepalives.
        VLOG(1) << "LLDP neighbor on " << obs.localIfname << " — non-MAC identity, not cached";
        return false;
    }

    std::scoped_lock lk(mutex_);
    auto &ifaceCache = byInterface_[obs.localIfname];
    if (obs.event == ObservationEvent::Removed) {
        VLOG(1) << "LLDP cache erase: ifname=" << obs.localIfname << " key=" << key;
        ifaceCache.erase(key);
    } else {
        VLOG(1) << "LLDP cache insert: ifname=" << obs.localIfname << " key=" << key;
        ifaceCache[key] = CachedLldpNeighbor{
            obs.remoteChassisId, obs.remotePortId, obs.remoteSystemName
        };
    }
    ++generation_;
    return true;
}

std::vector<CachedLldpNeighbor>
LldpNeighborCache::flushInterface(const std::string &ifname) {
    std::scoped_lock lk(mutex_);
    auto it = byInterface_.find(ifname);
    if (it == byInterface_.end()) {
        // No bump: nothing moved, and a spurious bump would make a
        // concurrent keepalive batch abandon itself for no reason.
        return {};
    }

    std::vector<CachedLldpNeighbor> removed;
    removed.reserve(it->second.size());
    for (auto &[_, entry]: it->second) {
        removed.push_back(std::move(entry));
    }
    byInterface_.erase(it);
    ++generation_;
    return removed;
}

NeighborCacheMap LldpNeighborCache::exchange(NeighborCacheMap replacement) {
    std::scoped_lock lk(mutex_);
    NeighborCacheMap previous = std::move(byInterface_);
    byInterface_ = std::move(replacement);
    ++generation_;
    return previous;
}

void LldpNeighborCache::clear() {
    std::scoped_lock lk(mutex_);
    if (byInterface_.empty()) {
        return;
    }
    byInterface_.clear();
    ++generation_;
}

std::pair<NeighborCacheMap, std::uint64_t> LldpNeighborCache::snapshot() const {
    std::scoped_lock lk(mutex_);
    // Map and generation must be read together — a caller comparing a
    // separately-fetched generation could not tell whether it preceded or
    // followed the copy.
    return {byInterface_, generation_};
}

std::uint64_t LldpNeighborCache::generation() const {
    std::scoped_lock lk(mutex_);
    return generation_;
}

std::vector<std::pair<std::string, CachedLldpNeighbor> >
diffRemovedNeighbors(const NeighborCacheMap &oldCache, const NeighborCacheMap &fresh) {
    std::vector<std::pair<std::string, CachedLldpNeighbor> > removed;
    for (const auto &[ifname, neighbors]: oldCache) {
        const auto freshIt = fresh.find(ifname);
        for (const auto &[key, entry]: neighbors) {
            const bool reSeen = freshIt != fresh.end() && freshIt->second.contains(key);
            if (reSeen)
                continue;
            removed.emplace_back(ifname, entry);
        }
    }
    return removed;
}

} // namespace RSCGroup