//
// Created by vvass on 20-Jul-26.
//
#pragma once
#include <string_view>

namespace RSCGroup {

class IInterfacePolicy {
public:
    virtual ~IInterfacePolicy() = default;

    virtual bool includeInLocalState(std::string_view ifname) const = 0;
    virtual bool allowRemoteNeighborEvidence(std::string_view ifname) const = 0;
    virtual bool allowRemoteFdbEvidence(std::string_view ifname) const = 0;
    virtual bool allowLldpEvidence(std::string_view ifname) const = 0;
};

} // namespace RSCGroup