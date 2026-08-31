//
// Created by vvass on 20-Jul-26.
//
/**
 * @file NetlinkState.h
 * @brief State management and MAC-based aggregation for network observations.
 *
 * @note The current data model keys FDB entries by MAC address only
 * (fdbStatesByMac_). This conflates multi-port sightings of the same
 * MAC when a device moves or is visible via multiple bridge ports.
 * Consumers should be aware that a MAC may be reported on only the
 * most recently observed port.
 */
#pragma once
#include "NetlinkTypes.h"
#include "NetlinkUtils.h"
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace RSCGroup {

using DeviceEventCallback = std::function<void(const DeviceEvent&)>;

struct MacAggregate {
    std::optional<FdbEvent> fdb;
    std::set<std::string> ipv4Addrs;
    std::set<std::string> ipv6Addrs;
};

class NetlinkState {
public:
    NetlinkState() = default;

    std::optional<LinkEvent> updateLink(const LinkEvent& event);
    std::optional<InterfaceIpEvent> updateAddress(const InterfaceIpEvent& event);
    std::optional<FdbEvent> updateFdb(const FdbEvent& event, DeviceEventCallback onDevice);
    std::optional<NeighborEvent> updateNeighbor(const NeighborEvent& event, DeviceEventCallback onDevice);

    std::vector<DeviceEvent> getDevicesSnapshot() const;
    std::vector<LinkEvent> getLinksSnapshot() const;
    void clear();

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, LinkEvent> linkStates_;
    std::unordered_map<InterfaceAddressKey, bool, InterfaceAddressKeyHash> interfaceAddresses_;
    std::unordered_map<std::string, FdbEvent> fdbStatesByMac_;
    std::unordered_map<std::string, NeighborEvent> neighborStatesByKey_;
    std::unordered_map<std::string, MacAggregate> macAggregates_;
    std::unordered_map<std::string, DeviceEvent> deviceStatesByMac_;

    std::optional<DeviceEvent> refreshMergedDeviceLocked(const std::string& mac);
};

} // namespace RSCGroup