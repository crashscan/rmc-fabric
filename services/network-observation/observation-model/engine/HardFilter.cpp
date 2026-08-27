//
// Created by vvass on 20-Jul-26.
//
#include "HardFilter.h"
#include <arpa/inet.h>
#include <cstdlib>
#include <string>

namespace RSCGroup {

HardFilter::HardFilter(const ModelConfig& config)
    : config_(config)
{}

bool HardFilter::passes(const NeighborObservation& obs, bool isLocalMac, bool isLocalIp) const {
    if (config_.skipNullMac && isNullMac(obs.mac)) return false;
    if (config_.skipLoopbackInterface && obs.ifname == "lo") return false;
    if (config_.skipMulticastIPv4 && isMulticastIPv4(obs.ip)) return false;
    if (config_.skipMulticastIPv6 && isMulticastIPv6(obs.ip)) return false;
    if (config_.skipMulticastMac && isMulticastMac(obs.mac)) return false;
    if (config_.skipIeeeReservedMac && isIeeeReservedMac(obs.mac)) return false;
    if (isLocalMac) return false;
    if (isLocalIp) return false;
    return true;
}

bool HardFilter::passes(const FdbObservation& obs, bool isLocalMac) const {
    if (config_.skipNullMac && isNullMac(obs.mac)) return false;
    if (config_.skipMulticastMac && isMulticastMac(obs.mac)) return false;
    if (config_.skipIeeeReservedMac && isIeeeReservedMac(obs.mac)) return false;
    if (isLocalMac) return false;
    if (obs.entryKind == FdbEntryKind::Local || obs.entryKind == FdbEntryKind::ControlPlane)
        return false;
    return true;
}

bool HardFilter::isNullMac(std::string_view mac) {
    return mac == "00:00:00:00:00:00";
}

bool HardFilter::isMulticastMac(std::string_view mac) {
    if (mac.size() < 2) return false;
    // Parse first two hex chars manually — sscanf(mac.data()) is unsafe
    // because string_view is not guaranteed null-terminated.
    char hexPair[3] = {mac[0], mac[1], '\0'};
    char* end = nullptr;
    long firstOctet = std::strtol(hexPair, &end, 16);
    if (end == hexPair + 2)
        return (firstOctet & 0x01) != 0;
    return false;
}

bool HardFilter::isIeeeReservedMac(std::string_view mac) {
    return mac.starts_with("01:80:c2");
}

bool HardFilter::isMulticastIPv4(std::string_view ip) {
    struct in_addr addr;
    std::string ipStr(ip);
    if (inet_pton(AF_INET, ipStr.c_str(), &addr) == 1) {
        uint32_t n = ntohl(addr.s_addr);
        return (n >= 0xE0000000 && n <= 0xEFFFFFFF);
    }
    return false;
}

bool HardFilter::isMulticastIPv6(std::string_view ip) {
    return ip.starts_with("ff");
}

} // namespace RSCGroup