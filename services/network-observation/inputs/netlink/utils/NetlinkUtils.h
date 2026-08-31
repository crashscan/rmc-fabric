//
// Created by vvass on 20-Jul-26.
//
/**
 * @file NetlinkUtils.h
 * @brief Utility functions and types for netlink message processing.
 */

#pragma once

#include <string>

namespace RSCGroup {

std::string ifIndexToName(unsigned int ifindex);
void invalidateIfIndex(unsigned int ifindex);
void updateIfIndexName(unsigned int ifindex, std::string ifname);
std::string formatIpAddress(int family, const void* data);
std::string formatMacAddress(const unsigned char* data, std::size_t len);
bool isIpv4(int family);
bool isIpv6(int family);
bool isFdbLocal(unsigned short state);
bool isFdbPermanent(unsigned short state);

struct InterfaceAddressKey {
    std::string ifname;
    int family = 0;
    std::string address;
    unsigned char prefixLen = 0;
    bool operator==(const InterfaceAddressKey& other) const = default;
};

struct InterfaceAddressKeyHash {
    std::size_t operator()(const InterfaceAddressKey& key) const;
};

std::string makeNeighborKey(const std::string& ifname, const std::string& mac, int family, const std::string& ip);

} // namespace RSCGroup