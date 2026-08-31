//
// Created by vvass on 20-Jul-26.
//
/**
 * @file LocalStateTracker.h
 * @brief Tracks local interface and address state.
 *
 * Maintains refcounts for MAC and IP ownership so that
 * localMacs / localIps remain correct when interfaces are
 * removed, MACs change, or IPs are added/removed.
 */
#pragma once
#include "LocalStateTypes.h"
#include "ObservationTypes.h"
#include <string>
#include <string_view>
#include <unordered_map>

namespace RSCGroup {

class LocalStateTracker {
public:
    LocalStateTracker() = default;

    bool onLinkObservation(const LinkObservation& obs);
    bool onAddressObservation(const AddressObservation& obs);

    /// Reset all interface state and MAC/IP refcounts (source restart).
    void clear();

    const LocalNetworkSnapshot& snapshot() const { return snapshot_; }

    bool isLocalMac(std::string_view mac) const;
    bool isLocalIp(std::string_view ip) const;

private:
    LocalNetworkSnapshot snapshot_;

    // Refcounts — source of truth for localMacs / localIps
    std::unordered_map<std::string, int> localMacRefcount_;
    std::unordered_map<std::string, int> localIpRefcount_;

    void incrementMac(const std::string& mac);
    void decrementMac(const std::string& mac);
    void incrementIp(const std::string& ip);
    void decrementIp(const std::string& ip);
};

} // namespace RSCGroup