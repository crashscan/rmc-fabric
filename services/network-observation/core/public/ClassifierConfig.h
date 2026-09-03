//
// Created by vvass on 20-Jul-26.
//
#pragma once

namespace RSCGroup {

struct ScoringWeights {
    int unicastMac = 20;
    int ipv4 = 15;
    int ipv6 = 10;
    int neighbor = 15;
    int fdb = 25;
    int lldp = 40;   // INERT: seenInLldp short-circuits to TopologyPeer;
                     // kept for v2 non-MAC LLDP identity scoring
    int temporalBonus = 10;
    int agingPenalty = -15;
};

struct ScoringThresholds {
    int probable = 25;
    int confirmed = 50;
    int topology = 80;
};

enum class ClassifierKind { RuleBased, Scoring };

struct ClassifierConfig {
    ClassifierKind kind = ClassifierKind::RuleBased;
    ScoringWeights weights = {};
    ScoringThresholds thresholds = {};
};

} // namespace RSCGroup