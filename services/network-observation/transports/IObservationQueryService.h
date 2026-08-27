//
// Created by vvass on 25-Jul-26.
//
/**
 * @file IObservationQueryService.h
 * @brief Query interface that transports use to read observation state.
 *
 * Part of the transports layer — owned by the transport module, not
 * the application. Transports depend on this abstraction, not on
 * concrete services. ObservationService implements it.
 */
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

    virtual LocalNetworkSnapshot localSnapshot() const = 0;
    virtual std::optional<LocalInterfaceState> getInterface(const std::string& ifname) const = 0;
    virtual std::vector<RemoteCandidate> remoteCandidates() const = 0;
    virtual std::optional<RemoteCandidate> getCandidateByMac(const std::string& mac) const = 0;
    virtual bool isReady() const = 0;
};

} // namespace RSCGroup