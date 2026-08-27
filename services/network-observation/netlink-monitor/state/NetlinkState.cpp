//
// Created by vvass on 20-Jul-26.
//
/**
 * @file NetlinkState.cpp
 * @brief Implementations of state management and MAC-based aggregation.
 */
#include "NetlinkState.h"
#include "NetlinkUtils.h"
#include <algorithm>
#include <ranges>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace RSCGroup {

std::optional<LinkEvent> NetlinkState::updateLink(const LinkEvent& event)
{
    std::scoped_lock lock(mutex_);
    if (!event.present) {
        const bool hadLink = linkStates_.contains(event.ifname);
        linkStates_.erase(event.ifname);
        const auto removed = std::erase_if(interfaceAddresses_, [&](const auto& kv) {
            return kv.first.ifname == event.ifname;
        });
        if (hadLink || removed > 0) return event;
        return std::nullopt;
    }
    const auto it = linkStates_.find(event.ifname);
    const bool changed = (it == linkStates_.end()) || (it->second != event);
    linkStates_[event.ifname] = event;
    if (changed) {
        return event;
    }
    return std::nullopt;
}

std::optional<InterfaceIpEvent> NetlinkState::updateAddress(const InterfaceIpEvent& event)
{
    const InterfaceAddressKey key{
        event.ifname,
        event.family,
        event.address,
        event.prefixLen
    };

    std::scoped_lock lock(mutex_);
    bool changed{false};
    if (!event.present) {
        if (auto it = interfaceAddresses_.find(key); it != interfaceAddresses_.end()) {
            interfaceAddresses_.erase(it);
            changed = true;
        }
    } else {
        auto [_, inserted] = interfaceAddresses_.emplace(key, true);
        changed = inserted;
    }

    if (changed) {
        return event;
    }
    return std::nullopt;
}

std::optional<FdbEvent> NetlinkState::updateFdb(const FdbEvent& event, DeviceEventCallback onDevice)
{
    std::optional<FdbEvent> eventToEmit;
    std::optional<DeviceEvent> deviceEmit;
    {
        std::scoped_lock lock(mutex_);
        const auto it = fdbStatesByMac_.find(event.mac);
        const bool changed = (it == fdbStatesByMac_.end()) || (it->second != event);

        auto& agg = macAggregates_[event.mac];
        if (!event.present) {
            fdbStatesByMac_.erase(event.mac);
            agg.fdb.reset();
        } else {
            fdbStatesByMac_[event.mac] = event;
            agg.fdb = event;
        }

        if (changed) {
            eventToEmit = event;
        }

        deviceEmit = refreshMergedDeviceLocked(event.mac);
    }

    if (deviceEmit && onDevice) {
        onDevice(*deviceEmit);
    }
    return eventToEmit;
}

std::optional<NeighborEvent> NetlinkState::updateNeighbor(
    const NeighborEvent& event,
    DeviceEventCallback onDevice)
{
    const std::string key = makeNeighborKey(event.ifname, event.mac, event.family, event.ip);

    std::optional<NeighborEvent> eventToEmit;
    std::optional<DeviceEvent> deviceEmit;
    {
        std::scoped_lock lock(mutex_);

        bool changed = false;
        auto& agg = macAggregates_[event.mac];

        if (!event.present) {
            if (auto it = neighborStatesByKey_.find(key); it != neighborStatesByKey_.end()) {
                neighborStatesByKey_.erase(it);
                changed = true;
            }
            if (isIpv4(event.family)) {
                agg.ipv4Addrs.erase(event.ip);
            } else if (isIpv6(event.family)) {
                agg.ipv6Addrs.erase(event.ip);
            }
        } else {
            auto it = neighborStatesByKey_.find(key);
            changed = (it == neighborStatesByKey_.end()) || (it->second != event);
            neighborStatesByKey_[key] = event;
            if (isIpv4(event.family)) {
                agg.ipv4Addrs.insert(event.ip);
            } else if (isIpv6(event.family)) {
                agg.ipv6Addrs.insert(event.ip);
            }
        }

        if (changed) {
            eventToEmit = event;
        }

        deviceEmit = refreshMergedDeviceLocked(event.mac);
    }

    if (deviceEmit && onDevice) {
        onDevice(*deviceEmit);
    }
    return eventToEmit;
}

std::optional<DeviceEvent> NetlinkState::refreshMergedDeviceLocked(const std::string& mac)
{
    auto aggIt = macAggregates_.find(mac);
    if (aggIt == macAggregates_.end()) {
        if (auto existing = deviceStatesByMac_.find(mac); existing != deviceStatesByMac_.end()) {
            DeviceEvent removedEvent{
                existing->second.ifname,
                existing->second.mac,
                {},
                {},
                false
            };
            deviceStatesByMac_.erase(existing);
            return removedEvent;
        }
        return std::nullopt;
    }

    const MacAggregate& agg = aggIt->second;

    DeviceEvent merged{};
    merged.mac = mac;

    if (agg.fdb.has_value()) {
        merged.ifname = agg.fdb->ifname;
        merged.present = agg.fdb->present;
    }

    merged.ipv4Addrs = agg.ipv4Addrs;
    merged.ipv6Addrs = agg.ipv6Addrs;

    const bool hasAnyInfo =
        merged.present
        || !merged.ipv4Addrs.empty()
        || !merged.ipv6Addrs.empty()
        || !merged.ifname.empty();

    if (!hasAnyInfo) {
        macAggregates_.erase(aggIt);
        if (auto existing = deviceStatesByMac_.find(mac); existing != deviceStatesByMac_.end()) {
            DeviceEvent removedEvent{
                existing->second.ifname,
                existing->second.mac,
                {},
                {},
                false
            };
            deviceStatesByMac_.erase(existing);
            return removedEvent;
        }
        return std::nullopt;
    }

    auto it = deviceStatesByMac_.find(mac);
    const bool changed = (it == deviceStatesByMac_.end()) || (it->second != merged);
    deviceStatesByMac_[mac] = merged;
    if (changed) {
        return merged;
    }
    return std::nullopt;
}

std::vector<DeviceEvent> NetlinkState::getDevicesSnapshot() const
{
    std::vector<DeviceEvent> out;
    {
        std::scoped_lock lock(mutex_);
        out.reserve(deviceStatesByMac_.size());
        for (const auto& [_, device] : deviceStatesByMac_) {
            out.push_back(device);
        }
    }
    std::ranges::sort(out, {}, [](const DeviceEvent& d) {
        return std::pair{d.ifname, d.mac};
    });
    return out;
}

std::vector<LinkEvent> NetlinkState::getLinksSnapshot() const
{
    std::vector<LinkEvent> out;
    {
        std::scoped_lock lock(mutex_);
        out.reserve(linkStates_.size());
        for (const auto& [_, link] : linkStates_) {
            out.push_back(link);
        }
    }
    std::ranges::sort(out, {}, &LinkEvent::ifname);
    return out;
}

void NetlinkState::clear()
{
    std::scoped_lock lock(mutex_);
    linkStates_.clear();
    interfaceAddresses_.clear();
    fdbStatesByMac_.clear();
    neighborStatesByKey_.clear();
    macAggregates_.clear();
    deviceStatesByMac_.clear();
}

} // namespace RSCGroup