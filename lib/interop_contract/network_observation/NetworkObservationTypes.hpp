#pragma once

#include <optional>
#include <set>
#include <string>
#include <unordered_map>

namespace interop_contract::network_observation {

enum class CandidateClassification {
    Artifact,
    LocalSelf,
    WeakCandidate,
    ProbableEndpoint,
    RemoteEndpoint,
    GatewayLike,
    TopologyPeer,
    Unknown
};

enum class CandidateStatus {
    Provisional,
    Confirmed,
    Aged,
    Expired,
    Removed
};

/// Local interface state as serialised over D-Bus.
struct LocalInterfaceState {
    int ifindex = 0;
    std::string ifname;
    std::string mac;
    bool adminUp = false;
    bool running = false;
    std::string operstate;
    std::optional<std::string> masterIfname;
    std::set<std::string> ipv4;
    std::set<std::string> ipv6;
};

/// Network snapshot as serialised over D-Bus (interfaces only).
struct LocalNetworkSnapshot {
    std::unordered_map<std::string, LocalInterfaceState> interfaces;
};

/// Remote candidate as serialised over D-Bus (wire fields only).
struct RemoteCandidate {
    std::string mac;
    CandidateClassification classification = CandidateClassification::Unknown;
    CandidateStatus status = CandidateStatus::Provisional;
    bool seenInFdb   = false;
    bool seenInNeigh = false;
    bool seenInLldp  = false;
    std::optional<std::string> bridgePort;
    std::optional<std::string> remoteChassisId;
    std::optional<std::string> remotePortId;
    std::optional<std::string> remoteSystemName;
    std::set<std::string> neighborIfaces;
    std::set<std::string> ipv4;
    std::set<std::string> ipv6;
};

} // namespace interop_contract::network_observation
