//
// Created by vvass on 20-Jul-26.
//
#include "ScoringClassifier.h"
#include <string>

namespace RSCGroup {

ScoringClassifier::ScoringClassifier(int probable, int confirmed, int topology)
    : probableThreshold_(probable)
    , confirmedThreshold_(confirmed)
    , topologyThreshold_(topology)
{}

ScoringClassifier::ScoringClassifier(const ScoringWeights& weights, const ScoringThresholds& thresholds)
    : weights_(weights)
    , probableThreshold_(thresholds.probable)
    , confirmedThreshold_(thresholds.confirmed)
    , topologyThreshold_(thresholds.topology)
{}

CandidateClassification ScoringClassifier::classify(const RemoteCandidate& candidate) {
    // Bundled-classifier invariant (see ICandidateClassifier.h):
    // LLDP evidence means a physically-attached peer, regardless of score.
    if (candidate.seenInLldp)
        return CandidateClassification::TopologyPeer;
    return scoreToClass(computeScore(candidate));
}

int ScoringClassifier::computeScore(const RemoteCandidate& candidate) const {
    int score = 0;
    if (candidate.seenInFdb)   score += weights_.fdb;
    if (candidate.seenInNeigh) score += weights_.neighbor;
    if (candidate.seenInLldp)  score += weights_.lldp;
    if (!candidate.ipv4.empty()) score += weights_.ipv4;
    if (!candidate.ipv6.empty()) score += weights_.ipv6;
    return score;
}

CandidateClassification ScoringClassifier::scoreToClass(int score) const {
    if (score >= topologyThreshold_)  return CandidateClassification::TopologyPeer;
    if (score >= confirmedThreshold_) return CandidateClassification::RemoteEndpoint;
    if (score >= probableThreshold_)  return CandidateClassification::ProbableEndpoint;
    return CandidateClassification::WeakCandidate;
}

} // namespace RSCGroup