//
// Created by vvass on 20-Jul-26.
//
#pragma once
#include "ICandidateClassifier.h"
#include "ClassifierFactory.h"

namespace RSCGroup {

class ScoringClassifier : public ICandidateClassifier {
public:
    explicit ScoringClassifier(int probable = 25, int confirmed = 50, int topology = 80);

    /// Construct with full weights and thresholds from ClassifierFactoryConfig
    explicit ScoringClassifier(const ScoringWeights& weights, const ScoringThresholds& thresholds);

    CandidateClassification classify(const RemoteCandidate& candidate) override;
    std::string name() const override { return "scoring"; }

private:
    ScoringWeights weights_;
    int probableThreshold_;
    int confirmedThreshold_;
    int topologyThreshold_;

    int computeScore(const RemoteCandidate& candidate) const;
    CandidateClassification scoreToClass(int score) const;
};

} // namespace RSCGroup