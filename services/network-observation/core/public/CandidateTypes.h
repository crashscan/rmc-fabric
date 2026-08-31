//
// Created by vvass on 20-Jul-26.
//
/**
 * @file CandidateTypes.h
 * @brief Remote candidate types for the network observation model.
 *
 * CandidateClassification and CandidateStatus are defined in
 * lib/interop_contract/network_observation/ and re-exported here.
 * RSCGroup::RemoteCandidate is the full internal type; the wire-only
 * version is interop_contract::network_observation::RemoteCandidate.
 */
#pragma once

#include <interop_contract/network_observation/NetworkObservationTypes.hpp>

#include <chrono>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace RSCGroup {

using interop_contract::network_observation::CandidateClassification;
using interop_contract::network_observation::CandidateStatus;

struct CandidateScore {
    int total = 0;
    std::vector<std::string> reasons;
};

/// Per-source neighbor evidence key: (ifname, family, ip)
struct NeighborEvidenceKey {
    std::string ifname;
    int family = 0;
    std::string ip;
    bool operator==(const NeighborEvidenceKey& o) const = default;
    bool operator<(const NeighborEvidenceKey& o) const {
        if (ifname != o.ifname) return ifname < o.ifname;
        if (family != o.family) return family < o.family;
        return ip < o.ip;
    }
};

/// Per-source FDB evidence key: (ifname, mac) — mac is the remote MAC
struct FdbEvidenceKey {
    std::string ifname;
    std::string mac;
    bool operator==(const FdbEvidenceKey& o) const = default;
    bool operator<(const FdbEvidenceKey& o) const {
        if (ifname != o.ifname) return ifname < o.ifname;
        return mac < o.mac;
    }
};

/// Full internal remote candidate — extends the D-Bus wire type with
/// per-source evidence sets and model-internal tracking fields.
struct RemoteCandidate {
    std::string mac;
    std::set<std::string> ipv4;
    std::set<std::string> ipv6;
    std::optional<std::string> bridgePort;
    std::set<std::string> neighborIfaces;

    // Per-source evidence sets — source of truth for seenIn* booleans
    std::set<NeighborEvidenceKey> neighborEvidence;
    std::set<FdbEvidenceKey>       fdbEvidence;

    // Derived booleans (computed from evidence sets)
    bool seenInNeigh = false;
    bool seenInFdb   = false;
    bool seenInLldp  = false;

    std::optional<std::string> remoteChassisId;
    std::optional<std::string> remotePortId;
    std::optional<std::string> remoteSystemName;
    std::chrono::steady_clock::time_point firstSeen;
    std::chrono::steady_clock::time_point lastSeen;
    CandidateScore score;
    CandidateClassification classification = CandidateClassification::Unknown;
    CandidateStatus status = CandidateStatus::Provisional;
};

} // namespace RSCGroup