//
// Created by vvass on 20-Jul-26.
//
#pragma once
#include "ICandidateClassifier.h"

namespace RSCGroup {

class RuleBasedClassifier : public ICandidateClassifier {
public:
    CandidateClassification classify(const RemoteCandidate& candidate) override;
    std::string name() const override { return "rule-based"; }
};

} // namespace RSCGroup