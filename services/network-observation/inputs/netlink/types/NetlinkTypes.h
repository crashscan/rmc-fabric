//
// Created by vvass on 07-Jul-26.
//
/**
 * @file NetlinkTypes.h
 * @brief Event types and callbacks for NetlinkNetworkMonitor.
 *
 * Extracted from NetlinkNetworkMonitor.h during refactoring.
 */

#pragma once

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace RSCGroup {

/**
 * @brief Link (interface) state change event.
 */
struct LinkEvent
{
    /// Kernel interface index
    int ifindex = 0;
    /// Interface name (e.g. "eth0", "br-lan")
    std::string ifname;
    /// Hardware MAC address of the interface (xx:xx:xx:xx:xx:xx)
    std::string mac;
    /// Whether the interface is administratively up
    bool adminUp = false;
    /// Whether the interface has carrier/running
    bool running = false;
    /// RFC 2863 operstate (IF_OPER_UNKNOWN=0, IF_OPER_UP=6, etc.)
    unsigned char operState = 0;
    /// Ifindex of the master (bridge/bond) interface, if enslaved
    std::optional<int> masterIfindex;
    /// Name of the master interface, resolved from masterIfindex if available
    std::optional<std::string> masterIfname;
    /// True if link is present (RTM_NEWLINK), false if removed (RTM_DELLINK)
    bool present = true;
    bool operator==(const LinkEvent& other) const = default;
};

/**
 * @brief IP address added to or removed from an interface.
 */
struct InterfaceIpEvent
{
    /// Interface name
    std::string ifname;
    /// Address family (AF_INET or AF_INET6)
    int family = 0;
    /// IP address string (without prefix length)
    std::string address;
    /// Prefix length (e.g. 24 for /24)
    unsigned char prefixLen = 0;
    /// True if address was added, false if removed
    bool present = false;
    bool operator==(const InterfaceIpEvent& other) const = default;
};

/**
 * @brief Bridge FDB (forwarding database) entry event.
 */
struct FdbEvent
{
    /// Interface name the FDB entry was learned on
    std::string ifname;
    /// MAC address string (xx:xx:xx:xx:xx:xx)
    std::string mac;
    /// True if learned/present, false if removed
    bool present = false;
    /// True if entry is local to the bridge (NUD_NOARP)
    bool local = false;
    /// True if entry is permanent (NUD_PERMANENT)
    bool permanent = false;
    bool operator==(const FdbEvent& other) const = default;
};

/**
 * @brief L3 neighbor (ARP/NDP) entry event.
 */
struct NeighborEvent
{
    /// Interface name the neighbor was learned on
    std::string ifname;
    /// MAC address string
    std::string mac;
    /// Address family (AF_INET or AF_INET6)
    int family = 0;
    /// IP address string
    std::string ip;
    /// Raw NUD state from kernel (NUD_REACHABLE, NUD_STALE, etc.)
    unsigned short nudState = 0;
    /// True if learned/present, false if removed
    bool present = false;
    bool operator==(const NeighborEvent& other) const = default;
};

/**
 * @brief Aggregated device view (FDB + neighbor merged by MAC).
 */
struct DeviceEvent
{
    /// Interface the device was seen on (from FDB)
    std::string ifname;
    /// MAC address string
    std::string mac;
    /// Set of IPv4 addresses associated with this MAC
    std::set<std::string> ipv4Addrs;
    /// Set of IPv6 addresses associated with this MAC
    std::set<std::string> ipv6Addrs;
    /// True if device is currently present
    bool present = false;
    bool operator==(const DeviceEvent& other) const = default;
};

/**
 * @brief Callback functions for each event type.
 *
 * Set the callbacks you care about; unset callbacks are not invoked.
 */
struct MonitorCallbacks
{
    std::function<void(const LinkEvent&)> onLinkChanged;
    std::function<void(const InterfaceIpEvent&)> onInterfaceIpChanged;
    std::function<void(const FdbEvent&)> onFdbChanged;
    std::function<void(const NeighborEvent&)> onNeighborChanged;
    std::function<void(const DeviceEvent&)> onDeviceChanged;
};

} // namespace RSCGroup