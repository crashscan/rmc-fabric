//
// Created by vvass on 07-Jul-26.
//
/**
 * @file NetlinkNetworkMonitor.h
 * @brief Public API for netlink-based network state monitoring.
 *
 * @section threading Threading and callbacks
 * All callbacks (onLinkChanged, onInterfaceIpChanged, onFdbChanged,
 * onNeighborChanged, onDeviceChanged) are invoked synchronously on
 * the monitor's internal I/O thread. Slow callbacks will block
 * netlink message processing for all event types. Callbacks may
 * be delivered in any order relative to each other; in particular,
 * onDeviceChanged may fire before the underlying onFdbChanged or
 * onNeighborChanged callback for the same MAC.
 *
 * Callbacks must not call stop() or destroy the NetlinkNetworkMonitor
 * instance from the monitor I/O thread. Attempting to do so will
 * be rejected at runtime (logged as an error) and the stop request
 * will be ignored.
 *
 * @section snapshots Snapshot semantics
 * getDevicesSnapshot() and getLinksSnapshot() return a point-in-time
 * copy captured under an internal mutex. They do not reflect
 * concurrent updates that occur after the copy is made.
 */
#pragma once
#include "NetlinkTypes.h"
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace RSCGroup {

class NetlinkNetworkMonitor {
public:
    explicit NetlinkNetworkMonitor(MonitorCallbacks callbacks = {}, std::set<std::string> watchedInterfaces = {});
    ~NetlinkNetworkMonitor();
    NetlinkNetworkMonitor(const NetlinkNetworkMonitor&) = delete;
    NetlinkNetworkMonitor& operator=(const NetlinkNetworkMonitor&) = delete;

    [[nodiscard]] bool start();
    void stop();
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] std::vector<DeviceEvent> getDevicesSnapshot() const;
    [[nodiscard]] std::vector<LinkEvent> getLinksSnapshot() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace RSCGroup