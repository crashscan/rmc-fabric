//
// Created by vvass on 20-Jul-26.
//
#include "RuleBasedClassifier.h"

namespace RSCGroup {

CandidateClassification RuleBasedClassifier::classify(const RemoteCandidate& candidate) {
    // Bundled-classifier invariant (see ICandidateClassifier.h):
    // LLDP evidence alone suffices for TopologyPeer.
    if (candidate.seenInLldp)
        return CandidateClassification::TopologyPeer;
    if (candidate.seenInFdb && !candidate.ipv4.empty())
        return CandidateClassification::RemoteEndpoint;
    if (candidate.seenInFdb && !candidate.ipv6.empty())
        return CandidateClassification::RemoteEndpoint;
    if (candidate.seenInFdb)
        return CandidateClassification::RemoteEndpoint;
    if ((!candidate.ipv4.empty() || !candidate.ipv6.empty()) && candidate.seenInNeigh)
        return CandidateClassification::RemoteEndpoint;
    if (candidate.seenInNeigh)
        return CandidateClassification::WeakCandidate;
    return CandidateClassification::Unknown;
}

} // namespace RSCGroup