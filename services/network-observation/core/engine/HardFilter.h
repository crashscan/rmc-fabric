//
// Created by vvass on 20-Jul-26.
//
/**
 * @file HardFilter.h
 * @brief Stateless hard artifact filter for observations.
 */
#pragma once
#include "ModelConfig.h"
#include "ObservationTypes.h"
#include <string_view>

namespace RSCGroup {

class HardFilter {
public:
    explicit HardFilter(const ModelConfig& config);

    bool passes(const NeighborObservation& obs, bool isLocalMac, bool isLocalIp) const;
    bool passes(const FdbObservation& obs, bool isLocalMac) const;

private:
    const ModelConfig& config_;

    static bool isMulticastMac(std::string_view mac);
    static bool isNullMac(std::string_view mac);
    static bool isIeeeReservedMac(std::string_view mac);
    static bool isMulticastIPv4(std::string_view ip);
    static bool isMulticastIPv6(std::string_view ip);
};

} // namespace RSCGroup