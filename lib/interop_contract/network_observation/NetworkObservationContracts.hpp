#pragma once

#include <cstdint>
#include <string_view>

namespace interop_contract::network_observation {

inline constexpr std::uint32_t CONTRACT_VERSION = 1;

inline constexpr std::string_view SERVICE_NAME  = "org.rsc.NetworkObservation";
inline constexpr std::string_view OBJECT_PATH   = "/org/rsc/NetworkObservation";
inline constexpr std::string_view INTERFACE     = "org.rsc.NetworkObservation";

// Methods
inline constexpr std::string_view METHOD_GET_LOCAL_SNAPSHOT          = "GetLocalSnapshot";
inline constexpr std::string_view METHOD_GET_INTERFACE               = "GetInterface";
inline constexpr std::string_view METHOD_GET_REMOTE_CANDIDATE_MACS   = "GetRemoteCandidateMacs";
inline constexpr std::string_view METHOD_GET_CANDIDATE_BY_MAC        = "GetCandidateByMac";
inline constexpr std::string_view METHOD_GET_ISSUES                  = "GetIssues";
inline constexpr std::string_view METHOD_GET_READY                   = "GetReady";
inline constexpr std::string_view METHOD_GET_PHASE                   = "GetPhase";

// Signals
inline constexpr std::string_view SIGNAL_LOCAL_STATE_CHANGED  = "LocalStateChanged";
inline constexpr std::string_view SIGNAL_INTERFACE_CHANGED    = "InterfaceChanged";
inline constexpr std::string_view SIGNAL_INTERFACE_REMOVED    = "InterfaceRemoved";
inline constexpr std::string_view SIGNAL_CANDIDATE_CHANGED    = "CandidateChanged";
inline constexpr std::string_view SIGNAL_CANDIDATE_REMOVED    = "CandidateRemoved";
inline constexpr std::string_view SIGNAL_READY_CHANGED        = "ReadyChanged";

// Phase values
inline constexpr std::string_view PHASE_INITIALIZING = "initializing";
inline constexpr std::string_view PHASE_LIVE         = "live";
inline constexpr std::string_view PHASE_STOPPED      = "stopped";

// Issue keys (GetIssues inner maps)
inline constexpr std::string_view ISSUE_SEVERITY  = "severity";
inline constexpr std::string_view ISSUE_MESSAGE   = "message";
inline constexpr std::string_view ISSUE_COMPONENT = "component";
inline constexpr std::string_view ISSUE_OPERATION = "operation";
inline constexpr std::string_view ISSUE_CATEGORY  = "category";
inline constexpr std::string_view ISSUE_IDENTITY  = "identity";

// Severity values
inline constexpr std::string_view SEVERITY_ERROR   = "error";
inline constexpr std::string_view SEVERITY_WARNING = "warning";

// Stable issue codes
inline constexpr std::string_view ISSUE_CODE_RUNTIME_STOPPED = "observation.runtime.stopped";
inline constexpr std::string_view ISSUE_CODE_AGING_LOOP_STOPPED = "observation.worker.aging.stopped";
inline constexpr std::string_view ISSUE_CODE_LLDP_UNAVAILABLE = "observation.input.lldp.unavailable";

// Variant map field keys — local interface
inline constexpr std::string_view K_IFINDEX   = "ifindex";
inline constexpr std::string_view K_IFNAME    = "ifname";
inline constexpr std::string_view K_MAC       = "mac";
inline constexpr std::string_view K_ADMINUP   = "adminUp";
inline constexpr std::string_view K_RUNNING   = "running";
inline constexpr std::string_view K_OPERSTATE = "operstate";
inline constexpr std::string_view K_MASTER    = "master";
inline constexpr std::string_view K_IPV4      = "ipv4";
inline constexpr std::string_view K_IPV6      = "ipv6";

// Variant map field keys — remote candidate
inline constexpr std::string_view K_CLASSIFICATION     = "classification";
inline constexpr std::string_view K_STATUS             = "status";
inline constexpr std::string_view K_SEEN_IN_FDB        = "seenInFdb";
inline constexpr std::string_view K_SEEN_IN_NEIGH      = "seenInNeigh";
inline constexpr std::string_view K_SEEN_IN_LLDP       = "seenInLldp";
inline constexpr std::string_view K_BRIDGE_PORT        = "bridgePort";
inline constexpr std::string_view K_REMOTE_CHASSIS_ID  = "remoteChassisId";
inline constexpr std::string_view K_REMOTE_PORT_ID     = "remotePortId";
inline constexpr std::string_view K_REMOTE_SYSTEM_NAME = "remoteSystemName";
inline constexpr std::string_view K_NEIGHBOR_IFACES    = "neighborIfaces";

} // namespace interop_contract::network_observation
