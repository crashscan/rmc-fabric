//
// Created by vvass on 20-Jul-26.
//
/**
 * @file LocalStateTypes.h
 * @brief Local network state types for the observation model.
 *
 * Wire-compatible types (LocalInterfaceState) are defined in
 * lib/interop_contract/network_observation/ and re-exported here.
 * The extended LocalNetworkSnapshot (with internal tracking sets) is
 * defined below.
 */
#pragma once

#include <interop_contract/network_observation/NetworkObservationTypes.hpp>

#include <set>
#include <string>
#include <unordered_map>

namespace RSCGroup {

using interop_contract::network_observation::LocalInterfaceState;

/// Extended snapshot used internally by the observation model.
/// Adds localMacs/localIps tracking sets that are not part of the D-Bus wire
/// contract (LocalNetworkSnapshot in the interop_contract has only interfaces).
struct LocalNetworkSnapshot {
    std::unordered_map<std::string, LocalInterfaceState> interfaces;
    std::set<std::string> localMacs;
    std::set<std::string> localIps;
};

} // namespace RSCGroup