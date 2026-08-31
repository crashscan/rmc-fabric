//
// Created by vvass on 20-Jul-26.
//
/**
 * @file NetlinkParser.h
 * @brief Parsing functions that translate raw netlink messages into typed events.
 */

#pragma once

#include <functional>
#include <optional>
#include <string>

struct nlmsghdr;
struct ifinfomsg;
struct ifaddrmsg;
struct ndmsg;
struct rtattr;

namespace RSCGroup {

struct LinkEvent;
struct InterfaceIpEvent;
struct FdbEvent;
struct NeighborEvent;

using LinkEventCallback = std::function<void(const LinkEvent&)>;
using IpEventCallback = std::function<void(const InterfaceIpEvent&)>;
using FdbEventCallback = std::function<void(const FdbEvent&)>;
using NeighborEventCallback = std::function<void(const NeighborEvent&)>;

template <typename T>
const rtattr* attrBegin(const T* msg);

template <typename Msg>
const Msg* getPayloadAndAttrLen(const nlmsghdr* nh, int& attrLen);

void processMessage(
    const nlmsghdr* nh,
    LinkEventCallback onLink,
    IpEventCallback onIp,
    FdbEventCallback onFdb,
    NeighborEventCallback onNeigh);

void handleLink(const nlmsghdr* nh, LinkEventCallback onLink);
void handleAddr(const nlmsghdr* nh, IpEventCallback onIp);
void handleNeigh(const nlmsghdr* nh, FdbEventCallback onFdb, NeighborEventCallback onNeigh);
void handleFdb(const nlmsghdr* nh, const ndmsg& ndm, const std::string& ifname, int attrLen, FdbEventCallback onFdb);
void handleNeighborL3(const nlmsghdr* nh, const ndmsg& ndm, const std::string& ifname, int attrLen, NeighborEventCallback onNeigh);

} // namespace RSCGroup