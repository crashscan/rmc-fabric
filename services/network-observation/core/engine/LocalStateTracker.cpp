//
// Created by vvass on 20-Jul-26.
//
#include "LocalStateTracker.h"
#include <sys/socket.h>

namespace RSCGroup {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string extractIpFromCidr(const std::string& cidr)
{
    auto pos = cidr.find('/');
    if (pos == std::string::npos) {
        // Malformed CIDR — no slash separator
        return {};
    }
    return cidr.substr(0, pos);
}

void LocalStateTracker::incrementMac(const std::string& mac)
{
    if (mac.empty()) return;
    if (++localMacRefcount_[mac] == 1)
        snapshot_.localMacs.insert(mac);
}

void LocalStateTracker::decrementMac(const std::string& mac)
{
    if (mac.empty()) return;
    auto it = localMacRefcount_.find(mac);
    if (it == localMacRefcount_.end()) return;
    if (--it->second == 0) {
        localMacRefcount_.erase(it);
        snapshot_.localMacs.erase(mac);
    }
}

void LocalStateTracker::incrementIp(const std::string& ip)
{
    if (++localIpRefcount_[ip] == 1)
        snapshot_.localIps.insert(ip);
}

void LocalStateTracker::decrementIp(const std::string& ip)
{
    auto it = localIpRefcount_.find(ip);
    if (it == localIpRefcount_.end()) return;
    if (--it->second == 0) {
        localIpRefcount_.erase(it);
        snapshot_.localIps.erase(ip);
    }
}

// ---------------------------------------------------------------------------
// Link observations
// ---------------------------------------------------------------------------

bool LocalStateTracker::onLinkObservation(const LinkObservation& obs)
{
    if (obs.event == ObservationEvent::Removed) {
        // Remove interface and all its MAC + IP ownership
        auto it = snapshot_.interfaces.find(obs.ifname);
        if (it != snapshot_.interfaces.end()) {
            decrementMac(it->second.mac);
            for (const auto& cidr : it->second.ipv4) {
                if (auto ip = extractIpFromCidr(cidr); !ip.empty())
                    decrementIp(ip);
            }
            for (const auto& cidr : it->second.ipv6) {
                if (auto ip = extractIpFromCidr(cidr); !ip.empty())
                    decrementIp(ip);
            }
            snapshot_.interfaces.erase(it);
            return true;
        }
        // Interface not found — nothing to do
        return false;
    }

    // Present: update or create interface state
    auto& iface = snapshot_.interfaces[obs.ifname];
    const bool isNew = iface.ifname.empty();
    const std::string oldMac = iface.mac;

    iface.ifindex   = obs.ifindex;
    iface.ifname    = obs.ifname;
    iface.adminUp   = obs.adminUp;
    iface.running   = obs.running;
    iface.operstate = obs.operstate;
    iface.masterIfname = obs.masterIfname;

    // Track MAC change via refcount
    if (isNew) {
        iface.mac = obs.mac;
        incrementMac(obs.mac);
    } else if (oldMac != obs.mac) {
        decrementMac(oldMac);
        iface.mac = obs.mac;
        incrementMac(obs.mac);
    }

    return oldMac != obs.mac;
}

// ---------------------------------------------------------------------------
// Address observations
// ---------------------------------------------------------------------------

bool LocalStateTracker::onAddressObservation(const AddressObservation& obs)
{
    auto& iface = snapshot_.interfaces[obs.ifname];
    std::string ip = extractIpFromCidr(obs.cidr);
    if (ip.empty()) return false;

    if (obs.event == ObservationEvent::Present) {
        if (obs.family == AF_INET) {
            if (iface.ipv4.insert(obs.cidr).second) {
                incrementIp(ip);
                return true;
            }
        } else {
            if (iface.ipv6.insert(obs.cidr).second) {
                incrementIp(ip);
                return true;
            }
        }
    } else {
        // Removed — erase from set and decrement refcount
        if (obs.family == AF_INET) {
            if (iface.ipv4.erase(obs.cidr)) {
                decrementIp(ip);
                return true;
            }
        } else {
            if (iface.ipv6.erase(obs.cidr)) {
                decrementIp(ip);
                return true;
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Lookup helpers
// ---------------------------------------------------------------------------

bool LocalStateTracker::isLocalMac(std::string_view mac) const
{
    return snapshot_.localMacs.contains(std::string(mac));
}

bool LocalStateTracker::isLocalIp(std::string_view ip) const
{
    return snapshot_.localIps.contains(std::string(ip));
}

void LocalStateTracker::clear()
{
    snapshot_ = LocalNetworkSnapshot{};
    localMacRefcount_.clear();
    localIpRefcount_.clear();
}

} // namespace RSCGroup
