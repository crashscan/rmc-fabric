#include "ObservationModelEngine.h"
#include "RuleBasedClassifier.h"
#include "ScoringClassifier.h"
#include <gtest/gtest.h>

namespace RSCGroup {
namespace {

struct RecordingSink : IModelEventSink {
    std::vector<ModelEvent> events;
    void onModelEvent(const ModelEvent& e) override { events.push_back(e); }
    std::vector<std::string> macsOf(ModelEventKind k) const {
        std::vector<std::string> out;
        for (const auto& e : events)
            if (e.kind == k && e.mac) out.push_back(*e.mac);
        return out;
    }
};

NeighborObservation makeNeigh(const char* mac, const char* ip) {
    NeighborObservation o;
    o.kind = ObservationKind::Neighbor;
    o.ifname = "eth0";
    o.event = ObservationEvent::Present;
    o.family = AF_INET;
    o.mac = mac; o.ip = ip;
    o.observedAt = std::chrono::steady_clock::now();
    o.reachability = NeighborReachability::Reachable;
    return o;
}

LldpObservation makeLldp(const char* chassisMac) {
    LldpObservation o;
    o.kind = ObservationKind::Lldp;
    o.localIfname = "eth0";
    o.event = ObservationEvent::Present;
    o.observedAt = std::chrono::steady_clock::now();
    o.remoteChassisId = chassisMac;
    return o;
}

// #1 — post-startup candidate is promoted without markLive
TEST(EngineLifecycleTest, LivePhasePromotion) {
    ObservationModelEngine engine(ModelConfig{});
    engine.markLive();                       // enter Live with no candidates
    engine.onNeighborObservation(makeNeigh("aa:bb:cc:dd:ee:ff", "10.0.0.1"));
    auto c = engine.findCandidateByMac("aa:bb:cc:dd:ee:ff");
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->status, CandidateStatus::Confirmed);
}

// #2 — events on publishable transition and on evidence loss
TEST(EngineLifecycleTest, CandidateEventsEmitted) {
    ObservationModelEngine engine(ModelConfig{});
    RecordingSink sink;
    engine.setEventSink(&sink);
    engine.markLive();
    engine.onNeighborObservation(makeNeigh("aa:bb:cc:dd:ee:ff", "10.0.0.1"));
    EXPECT_EQ(sink.macsOf(ModelEventKind::CandidateUpdated),
              std::vector<std::string>{"aa:bb:cc:dd:ee:ff"});

    auto gone = makeNeigh("aa:bb:cc:dd:ee:ff", "10.0.0.1");
    gone.event = ObservationEvent::Removed;
    engine.onNeighborObservation(gone);
    EXPECT_EQ(sink.macsOf(ModelEventKind::CandidateRemoved),
              std::vector<std::string>{"aa:bb:cc:dd:ee:ff"});
}

// #4 — LLDP-only peer is publishable TopologyPeer (no neighbor baseline)
TEST(EngineLifecycleTest, LldpOnlyIsTopologyPeerAndPublishable) {
    ObservationModelEngine engine(ModelConfig{});
    engine.markLive();
    engine.onLldpObservation(makeLldp("aa:bb:cc:dd:ee:ff"));
    auto c = engine.findCandidateByMac("aa:bb:cc:dd:ee:ff");
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->classification, CandidateClassification::TopologyPeer);
}

// Classifier parity invariant
TEST(ClassifierParityTest, LldpAlwaysTopologyPeer) {
    RuleBasedClassifier rule;
    ScoringClassifier scoring;
    RemoteCandidate c; c.seenInLldp = true;
    EXPECT_EQ(rule.classify(c),    CandidateClassification::TopologyPeer);
    EXPECT_EQ(scoring.classify(c), CandidateClassification::TopologyPeer);
}

// #6 — restart: re-observed survives with firstSeen intact
TEST(EngineLifecycleTest, RestartReobservedSurvives) {
    ObservationModelEngine engine(ModelConfig{});
    engine.onNeighborObservation(makeNeigh("aa:bb:cc:dd:ee:ff", "10.0.0.1"));
    engine.markLive();
    const auto firstSeenBefore =
        engine.findCandidateByMac("aa:bb:cc:dd:ee:ff")->firstSeen;

    engine.prepareForRestart();
    EXPECT_FALSE(engine.findCandidateByMac("aa:bb:cc:dd:ee:ff").has_value());

    engine.onNeighborObservation(makeNeigh("aa:bb:cc:dd:ee:ff", "10.0.0.1"));
    engine.markLive();
    auto c = engine.findCandidateByMac("aa:bb:cc:dd:ee:ff");
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->firstSeen, firstSeenBefore);
}

// #6 — restart: ghost is erased at markLive (the tombstone trap)
TEST(EngineLifecycleTest, RestartGhostErased) {
    ObservationModelEngine engine(ModelConfig{});
    engine.onNeighborObservation(makeNeigh("aa:bb:cc:dd:ee:ff", "10.0.0.1"));
    engine.markLive();
    engine.prepareForRestart();
    engine.markLive();                       // no re-observation
    EXPECT_FALSE(engine.findCandidateByMac("aa:bb:cc:dd:ee:ff").has_value());
}

// #6 — restart emits Removed for previously-publishable candidates
TEST(EngineLifecycleTest, RestartEmitsRemovedFlush) {
    ObservationModelEngine engine(ModelConfig{});
    RecordingSink sink;
    engine.onNeighborObservation(makeNeigh("aa:bb:cc:dd:ee:ff", "10.0.0.1"));
    engine.markLive();
    engine.setEventSink(&sink);
    engine.prepareForRestart();
    EXPECT_EQ(sink.macsOf(ModelEventKind::CandidateRemoved),
              std::vector<std::string>{"aa:bb:cc:dd:ee:ff"});
}

// #6 — Removed tombstone revived by fresh evidence
TEST(EngineLifecycleTest, TombstoneRevival) {
    ObservationModelEngine engine(ModelConfig{});
    // Make IP local -> candidate reconciled to LocalSelf/Removed
    engine.onNeighborObservation(makeNeigh("aa:bb:cc:dd:ee:ff", "10.0.0.1"));
    engine.markLive();
    AddressObservation addr;
    addr.kind = ObservationKind::Address;
    addr.ifname = "eth0"; addr.event = ObservationEvent::Present;
    addr.family = AF_INET; addr.cidr = "10.0.0.1/24";
    addr.observedAt = std::chrono::steady_clock::now();
    engine.onAddressObservation(addr);
    EXPECT_FALSE(engine.findCandidateByMac("aa:bb:cc:dd:ee:ff").has_value());

    // IP removed locally; device reappears remotely
    addr.event = ObservationEvent::Removed;
    engine.onAddressObservation(addr);
    engine.onNeighborObservation(makeNeigh("aa:bb:cc:dd:ee:ff", "10.0.0.1"));
    auto c = engine.findCandidateByMac("aa:bb:cc:dd:ee:ff");
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->status, CandidateStatus::Confirmed);
}

} // namespace
} // namespace RSCGroup
