//
// Created by vvass on 20-Jul-26.
//
/**
 * @file LocalStateTypes.h
 * @brief Local network state types for the observation model.
 */
#pragma once
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace RSCGroup {

struct LocalInterfaceState {
    int ifindex = 0;
    std::string ifname;
    std::string mac;
    bool adminUp = false;
    bool running = false;
    std::string operstate;
    std::optional<std::string> masterIfname;
    std::set<std::string> ipv4;
    std::set<std::string> ipv6;
};

struct LocalNetworkSnapshot {
    std::unordered_map<std::string, LocalInterfaceState> interfaces;
    std::set<std::string> localMacs;
    std::set<std::string> localIps;
};

} // namespace RSCGroup