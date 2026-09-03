//
// Created by vvass on 20-Jul-26.
//
#pragma once
#include <IInterfacePolicy.h>
#include <string_view>

namespace RSCGroup {

class DefaultInterfacePolicy : public IInterfacePolicy {
public:
    bool includeInLocalState(std::string_view ifname) const override;
    bool allowRemoteNeighborEvidence(std::string_view ifname) const override;
    bool allowRemoteFdbEvidence(std::string_view ifname) const override;
    bool allowLldpEvidence(std::string_view ifname) const override;

private:
    static bool isExcluded(std::string_view ifname);
};

} // namespace RSCGroup