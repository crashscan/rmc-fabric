//
// Created by vvass on 20-Jul-26.
//
#include "ClassifierFactory.h"
#include "RuleBasedClassifier.h"
#include "ScoringClassifier.h"

namespace RSCGroup {

std::unique_ptr<ICandidateClassifier> createClassifier(const ClassifierFactoryConfig& cfg) {
    switch (cfg.kind) {
        case ClassifierKind::Scoring:
            return std::make_unique<ScoringClassifier>(cfg.weights, cfg.thresholds);
        case ClassifierKind::RuleBased:
        default:
            return std::make_unique<RuleBasedClassifier>();
    }
}

} // namespace RSCGroup