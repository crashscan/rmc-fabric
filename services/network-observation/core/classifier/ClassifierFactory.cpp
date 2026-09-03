//
// Created by vvass on 20-Jul-26.
//
#include "ClassifierFactory.h"
#include "RuleBasedClassifier.h"
#include "ScoringClassifier.h"

namespace RSCGroup {

std::unique_ptr<ICandidateClassifier> createClassifier(const ClassifierConfig& config) {
    switch (config.kind) {
        case ClassifierKind::Scoring:
            return std::make_unique<ScoringClassifier>(config.weights, config.thresholds);
        case ClassifierKind::RuleBased:
        default:
            return std::make_unique<RuleBasedClassifier>();
    }
}

} // namespace RSCGroup