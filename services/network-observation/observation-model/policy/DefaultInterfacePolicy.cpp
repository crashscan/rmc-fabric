//
// Created by vvass on 20-Jul-26.
//
#include "IInterfacePolicy.h"
#include <string>

namespace RSCGroup {

bool DefaultInterfacePolicy::isExcluded(std::string_view ifname) {
    if (ifname == "lo") return true;
    if (ifname.starts_with("can")) return true;
    if (ifname.starts_with("sit")) return true;
    if (ifname.starts_with("tun")) return true;
    if (ifname.starts_with("tap")) return true;
    if (ifname.starts_with("veth")) return true;
    if (ifname.starts_with("docker")) return true;
    return false;
}

bool DefaultInterfacePolicy::includeInLocalState(std::string_view ifname) const {
    return !isExcluded(ifname);
}

bool DefaultInterfacePolicy::allowRemoteNeighborEvidence(std::string_view ifname) const {
    return !isExcluded(ifname);
}

bool DefaultInterfacePolicy::allowRemoteFdbEvidence(std::string_view ifname) const {
    return !isExcluded(ifname);
}

bool DefaultInterfacePolicy::allowLldpEvidence(std::string_view ifname) const {
    if (isExcluded(ifname)) return false;
    if (ifname.starts_with("br-")) return false;
    return true;
}

} // namespace RSCGroup