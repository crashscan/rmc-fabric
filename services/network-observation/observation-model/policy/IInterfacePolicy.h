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