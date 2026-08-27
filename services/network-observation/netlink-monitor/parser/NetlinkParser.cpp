// SPDX-License-Identifier: MIT
#include "NetlinkParser.h"
#include "NetlinkTypes.h"
#include "NetlinkUtils.h"
#include <glog/logging.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <optional>
#include <string>

namespace RSCGroup {

template <typename T>
const rtattr* attrBegin(const T* msg) {
    return reinterpret_cast<const rtattr*>(
        reinterpret_cast<const char*>(msg) + NLMSG_ALIGN(sizeof(T)));
}

template <typename Msg>
const Msg* getPayloadAndAttrLen(const nlmsghdr* nh, int& attrLen) {
    constexpr int headerLen = static_cast<int>(NLMSG_LENGTH(sizeof(Msg)));
    const int messageLen = static_cast<int>(nh->nlmsg_len);
    if (messageLen < headerLen) {
        return nullptr;
    }
    attrLen = messageLen - headerLen;
    return reinterpret_cast<const Msg*>(NLMSG_DATA(nh));
}

template const rtattr* attrBegin<ifinfomsg>(const ifinfomsg*);
template const rtattr* attrBegin<ifaddrmsg>(const ifaddrmsg*);
template const rtattr* attrBegin<ndmsg>(const ndmsg*);

template const ifinfomsg* getPayloadAndAttrLen<ifinfomsg>(const nlmsghdr*, int&);
template const ifaddrmsg* getPayloadAndAttrLen<ifaddrmsg>(const nlmsghdr*, int&);
template const ndmsg* getPayloadAndAttrLen<ndmsg>(const nlmsghdr*, int&);

void processMessage(const nlmsghdr* nh,
                    LinkEventCallback onLink,
                    IpEventCallback onIp,
                    FdbEventCallback onFdb,
                    NeighborEventCallback onNeigh) {
    switch (nh->nlmsg_type) {
        case RTM_NEWLINK:
        case RTM_DELLINK:
            handleLink(nh, onLink);
            break;
        case RTM_NEWADDR:
        case RTM_DELADDR:
            handleAddr(nh, onIp);
            break;
        case RTM_NEWNEIGH:
        case RTM_DELNEIGH:
            handleNeigh(nh, onFdb, onNeigh);
            break;
        case NLMSG_ERROR: {
            // Validate minimum length before reading nlmsgerr
            if (nh->nlmsg_len < static_cast<int>(NLMSG_LENGTH(sizeof(nlmsgerr)))) {
                LOG(ERROR) << "received NLMSG_ERROR too short, len=" << nh->nlmsg_len;
                break;
            }
            const auto* err = reinterpret_cast<const nlmsgerr*>(NLMSG_DATA(nh));
            LOG(ERROR) << "received NLMSG_ERROR code=" << err->error;
            break;
        }
        default:
            VLOG(2) << "ignoring netlink message type=" << nh->nlmsg_type;
            break;
    }
}

void handleLink(const nlmsghdr* nh, LinkEventCallback onLink) {
    int attrLen{0};
    const auto* ifinfo = getPayloadAndAttrLen<ifinfomsg>(nh, attrLen);
    if (!ifinfo) {
        return;
    }

    LinkEvent event;
    event.ifindex = static_cast<int>(ifinfo->ifi_index);
    event.adminUp = (ifinfo->ifi_flags & IFF_UP) != 0;
    event.running = (ifinfo->ifi_flags & IFF_RUNNING) != 0;
    event.present = (nh->nlmsg_type != RTM_DELLINK);

    if (!event.present) {
        invalidateIfIndex(static_cast<unsigned int>(ifinfo->ifi_index));
    }

    bool hasIfname = false;

    for (auto* attr = attrBegin(ifinfo); RTA_OK(attr, attrLen); attr = RTA_NEXT(attr, attrLen)) {
        switch (attr->rta_type) {
            case IFLA_OPERSTATE:
                if (RTA_PAYLOAD(attr) >= sizeof(unsigned char)) {
                    event.operState = *static_cast<const unsigned char*>(RTA_DATA(attr));
                }
                break;
            case IFLA_ADDRESS:
                event.mac = formatMacAddress(
                    static_cast<const unsigned char*>(RTA_DATA(attr)),
                    RTA_PAYLOAD(attr));
                break;
            case IFLA_IFNAME: {
                if (RTA_PAYLOAD(attr) < 1) break;
                const char* p = static_cast<const char*>(RTA_DATA(attr));
                const int len = RTA_PAYLOAD(attr);
                // Trim trailing NUL only if present
                std::string name(p, p + len - (p[len - 1] == '\0' ? 1 : 0));
                event.ifname = name;
                updateIfIndexName(
                    static_cast<unsigned int>(ifinfo->ifi_index),
                    std::move(name));
                hasIfname = true;
                break;
            }
            case IFLA_MASTER:
                if (RTA_PAYLOAD(attr) >= sizeof(int)) {
                    const int masterIdx = *static_cast<const int*>(RTA_DATA(attr));
                    event.masterIfindex = masterIdx;
                    event.masterIfname = ifIndexToName(masterIdx);
                }
                break;
            default:
                break;
        }
    }

    if (!hasIfname) {
        event.ifname = ifIndexToName(static_cast<unsigned int>(ifinfo->ifi_index));
    }

    if (onLink) {
        onLink(event);
    }
}

void handleAddr(const nlmsghdr* nh, IpEventCallback onIp) {
    int attrLen{0};
    const auto* ifaddr = getPayloadAndAttrLen<ifaddrmsg>(nh, attrLen);
    if (!ifaddr) {
        return;
    }

    const bool v4 = isIpv4(ifaddr->ifa_family);
    const bool v6 = isIpv6(ifaddr->ifa_family);
    if (!v4 && !v6) return;

    const std::string ifname = ifIndexToName(ifaddr->ifa_index);
    const bool isDelete = (nh->nlmsg_type == RTM_DELADDR);

    struct AttrData {
        const void* data = nullptr;
        int len = 0;
    };

    AttrData v4local;
    AttrData v4addr;
    AttrData v6addr;

    for (auto* attr = attrBegin(ifaddr); RTA_OK(attr, attrLen); attr = RTA_NEXT(attr, attrLen)) {
        if (v4) {
            if (attr->rta_type == IFA_LOCAL) {
                v4local = { RTA_DATA(attr), RTA_PAYLOAD(attr) };
            } else if (attr->rta_type == IFA_ADDRESS) {
                v4addr = { RTA_DATA(attr), RTA_PAYLOAD(attr) };
            }
        } else if (v6 && attr->rta_type == IFA_ADDRESS) {
            v6addr = { RTA_DATA(attr), RTA_PAYLOAD(attr) };
        }
    }

    auto emitIp = [&](const AttrData& ad) {
        if (ad.len == 0) return;
        if (v4 && ad.len != 4) return;
        if (v6 && ad.len != 16) return;
        const std::string ip = formatIpAddress(ifaddr->ifa_family, ad.data);
        InterfaceIpEvent event{ ifname, ifaddr->ifa_family, ip, ifaddr->ifa_prefixlen, !isDelete };
        if (onIp) onIp(event);
    };

    if (v4local.data) {
        emitIp(v4local);
    } else if (v4addr.data) {
        emitIp(v4addr);
    }

    if (v6addr.data) {
        emitIp(v6addr);
    }
}

void handleNeigh(const nlmsghdr* nh,
                 FdbEventCallback onFdb,
                 NeighborEventCallback onNeigh) {
    int attrLen{0};
    const auto* ndm = getPayloadAndAttrLen<ndmsg>(nh, attrLen);
    if (!ndm) {
        return;
    }
    const std::string ifname = ifIndexToName(ndm->ndm_ifindex);

    if (ndm->ndm_family == AF_BRIDGE) {
        handleFdb(nh, *ndm, ifname, attrLen, onFdb);
        return;
    }

    if (isIpv4(ndm->ndm_family) || isIpv6(ndm->ndm_family)) {
        handleNeighborL3(nh, *ndm, ifname, attrLen, onNeigh);
    }
}

void handleFdb(const nlmsghdr* nh,
               const ndmsg& ndm,
               const std::string& ifname,
               int attrLen,
               FdbEventCallback onFdb) {
    std::optional<std::string> mac;
    for (auto* attr = attrBegin(&ndm); RTA_OK(attr, attrLen); attr = RTA_NEXT(attr, attrLen)) {
        if (attr->rta_type == NDA_LLADDR) {
            if (RTA_PAYLOAD(attr) < 6) continue;
            mac = formatMacAddress(
                static_cast<const unsigned char*>(RTA_DATA(attr)),
                RTA_PAYLOAD(attr));
        }
    }
    if (!mac) {
        return;
    }

    FdbEvent event{
        ifname,
        *mac,
        nh->nlmsg_type != RTM_DELNEIGH,
        isFdbLocal(ndm.ndm_state),
        isFdbPermanent(ndm.ndm_state)
    };

    if (onFdb) {
        onFdb(event);
    }
}

void handleNeighborL3(const nlmsghdr* nh,
                      const ndmsg& ndm,
                      const std::string& ifname,
                      int attrLen,
                      NeighborEventCallback onNeigh) {
    std::optional<std::string> mac;
    std::optional<std::string> ip;
    for (auto* attr = attrBegin(&ndm); RTA_OK(attr, attrLen); attr = RTA_NEXT(attr, attrLen)) {
        switch (attr->rta_type) {
            case NDA_LLADDR:
                if (RTA_PAYLOAD(attr) >= 6) {
                    mac = formatMacAddress(
                        static_cast<const unsigned char*>(RTA_DATA(attr)),
                        RTA_PAYLOAD(attr));
                }
                break;
            case NDA_DST:
                if ((ndm.ndm_family == AF_INET  && RTA_PAYLOAD(attr) == 4) ||
                    (ndm.ndm_family == AF_INET6 && RTA_PAYLOAD(attr) == 16)) {
                    ip = formatIpAddress(ndm.ndm_family, RTA_DATA(attr));
                }
                break;
            default:
                break;
        }
    }

    if (!mac || !ip) {
        return;
    }

    NeighborEvent event{
        ifname,
        *mac,
        ndm.ndm_family,
        *ip,
        ndm.ndm_state,
        nh->nlmsg_type != RTM_DELNEIGH
    };

    if (onNeigh) {
        onNeigh(event);
    }
}

} // namespace RSCGroup