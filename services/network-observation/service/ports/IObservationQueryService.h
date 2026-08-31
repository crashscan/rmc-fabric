#pragma once

#include "LocalStateTypes.h"
#include "CandidateTypes.h"

#include <optional>
#include <string>
#include <vector>

namespace RSCGroup {

class IObservationQueryService {
public:
    virtual ~IObservationQueryService() = default;

    [[nodiscard]] virtual LocalNetworkSnapshot localSnapshot() const = 0;
    [[nodiscard]] virtual std::optional<LocalInterfaceState> getInterface(const std::string& ifname) const = 0;
    [[nodiscard]] virtual std::vector<RemoteCandidate> remoteCandidates() const = 0;
    [[nodiscard]] virtual std::optional<RemoteCandidate> getCandidateByMac(const std::string& mac) const = 0;
    [[nodiscard]] virtual interop_contract::network_observation::ObservationIssues getIssues() const = 0;
    [[nodiscard]] virtual bool isReady() const = 0;
    [[nodiscard]] virtual std::string getPhase() const = 0;
};

} // namespace RSCGroup
