//
// Created by vvass on 20-Jul-26.
//
/**
 * @file NetlinkUtils.cpp
 * @brief Implementations of utility functions for netlink message processing.
 */

#include "NetlinkUtils.h"
#include "NetlinkTypes.h"

#include <arpa/inet.h>
#include <linux/rtnetlink.h>
#include <net/if.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

namespace RSCGroup {

namespace {

struct IfindexCache {
    std::string resolve(unsigned int ifindex) {
        {
            std::scoped_lock lock(mutex_);
            if (auto it = cache_.find(ifindex); it != cache_.end()) {
                return it->second;
            }
        }

        if (char ifname[IF_NAMESIZE] = {}; if_indextoname(ifindex, ifname)) {
            std::scoped_lock lock(mutex_);
            cache_[ifindex] = ifname;
            return ifname;
        }
        return std::to_string(ifindex);
    }

    void put(unsigned int ifindex, std::string ifname) {
        std::scoped_lock lock(mutex_);
        cache_[ifindex] = std::move(ifname);
    }

    void remove(unsigned int ifindex) {
        std::scoped_lock lock(mutex_);
        cache_.erase(ifindex);
    }

private:
    std::unordered_map<unsigned int, std::string> cache_;
    std::mutex mutex_;
};

IfindexCache& ifindexCache() {
    static IfindexCache cache;
    return cache;
}

}

std::string ifIndexToName(unsigned int ifindex) {
    return ifindexCache().resolve(ifindex);
}

void invalidateIfIndex(unsigned int ifindex) {
    ifindexCache().remove(ifindex);
}

void updateIfIndexName(unsigned int ifindex, std::string ifname) {
    ifindexCache().put(ifindex, std::move(ifname));
}

std::string formatIpAddress(int family, const void* data) {
    char buf[INET6_ADDRSTRLEN] = {};
    const char* p = inet_ntop(family, data, buf, sizeof(buf));
    return p ? std::string(p) : std::string("<invalid>");
}

std::string formatMacAddress(const unsigned char* data, std::size_t len) {
    if (len < 6) {
        return "<invalid>";
    }

    char buf[32] = {};
    std::snprintf(
        buf,
        sizeof(buf),
        "%02x:%02x:%02x:%02x:%02x:%02x",
        data[0], data[1], data[2], data[3], data[4], data[5]);
    return buf;
}

bool isIpv4(int family) { return family == AF_INET; }
bool isIpv6(int family) { return family == AF_INET6; }
bool isFdbLocal(unsigned short state) { return (state & NUD_NOARP) != 0; }
bool isFdbPermanent(unsigned short state) { return (state & NUD_PERMANENT) != 0; }

std::size_t InterfaceAddressKeyHash::operator()(const InterfaceAddressKey& key) const {
    std::size_t h = std::hash<std::string>{}(key.ifname);
    h ^= std::hash<int>{}(key.family) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<std::string>{}(key.address) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<unsigned int>{}(key.prefixLen) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

std::string makeNeighborKey(const std::string& ifname, const std::string& mac, int family, const std::string& ip) {
    return ifname + "|" + mac + "|" + std::to_string(family) + "|" + ip;
}

} // namespace RSCGroup