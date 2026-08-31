#pragma once

#include "NetworkObservationTypes.hpp"

#include <string>

namespace interop_contract::network_observation {

inline std::string classificationToString(CandidateClassification c)
{
    switch (c) {
        case CandidateClassification::Artifact:         return "Artifact";
        case CandidateClassification::LocalSelf:        return "LocalSelf";
        case CandidateClassification::WeakCandidate:    return "WeakCandidate";
        case CandidateClassification::ProbableEndpoint: return "ProbableEndpoint";
        case CandidateClassification::RemoteEndpoint:   return "RemoteEndpoint";
        case CandidateClassification::GatewayLike:      return "GatewayLike";
        case CandidateClassification::TopologyPeer:     return "TopologyPeer";
        case CandidateClassification::Unknown:          return "Unknown";
    }
    return "Unknown";
}

inline CandidateClassification classificationFromString(const std::string& s)
{
    if (s == "Artifact")         return CandidateClassification::Artifact;
    if (s == "LocalSelf")        return CandidateClassification::LocalSelf;
    if (s == "WeakCandidate")    return CandidateClassification::WeakCandidate;
    if (s == "ProbableEndpoint") return CandidateClassification::ProbableEndpoint;
    if (s == "RemoteEndpoint")   return CandidateClassification::RemoteEndpoint;
    if (s == "GatewayLike")      return CandidateClassification::GatewayLike;
    if (s == "TopologyPeer")     return CandidateClassification::TopologyPeer;
    return CandidateClassification::Unknown;
}

inline std::string statusToString(CandidateStatus s)
{
    switch (s) {
        case CandidateStatus::Provisional: return "Provisional";
        case CandidateStatus::Confirmed:   return "Confirmed";
        case CandidateStatus::Aged:        return "Aged";
        case CandidateStatus::Expired:     return "Expired";
        case CandidateStatus::Removed:     return "Removed";
    }
    return "Unknown";
}

inline CandidateStatus statusFromString(const std::string& s)
{
    if (s == "Provisional") return CandidateStatus::Provisional;
    if (s == "Confirmed")   return CandidateStatus::Confirmed;
    if (s == "Aged")        return CandidateStatus::Aged;
    if (s == "Expired")     return CandidateStatus::Expired;
    if (s == "Removed")     return CandidateStatus::Removed;
    return CandidateStatus::Provisional;
}

} // namespace interop_contract::network_observation
