//
// Created by vvass on 20-Jul-26.
//
#pragma once
#include "CandidateTypes.h"
#include <string>

namespace RSCGroup {

/// Contract for bundled classifiers:
///   seenInLldp == true  =>  TopologyPeer
/// Custom classifiers may deviate, but the bundled RuleBased and
/// Scoring classifiers must not diverge on this point.
class ICandidateClassifier {
public:
    virtual ~ICandidateClassifier() = default;

    virtual CandidateClassification classify(const RemoteCandidate& candidate) = 0;
    virtual std::string name() const = 0;
};

} // namespace RSCGroup