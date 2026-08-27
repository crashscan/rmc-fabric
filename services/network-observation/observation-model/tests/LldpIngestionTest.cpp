//
// Created by vvass on 24-Jul-26.
//
#include "ObservationModelEngine.h"
#include "ModelConfig.h"
#include "ObservationTypes.h"
#include <gtest/gtest.h>

namespace RSCGroup {
namespace {

TEST(LldpIngestionTest, ChassisIdMacMatch) {
    ModelConfig config;
    ObservationModelEngine engine(std::move(config));

    // Baseline: neighbor evidence makes candidate publishable
    NeighborObservation nobs;
    nobs.kind = ObservationKind::Neighbor;
    nobs.ifname = "eth0";
    nobs.event = ObservationEvent::Present;
    nobs.family = AF_INET;
    nobs.mac = "aa:bb:cc:dd:ee:ff";
    nobs.ip = "10.0.0.1";
    nobs.observedAt = std::chrono::steady_clock::now();
    nobs.reachability = NeighborReachability::Reachable;
    engine.onNeighborObservation(nobs);
    engine.markLive();

    auto before = engine.findCandidateByMac("aa:bb:cc:dd:ee:ff");
    ASSERT_TRUE(before.has_value());
    EXPECT_FALSE(before->seenInLldp);

    // Add LLDP with same MAC chassis ID
    LldpObservation lobs;
    lobs.kind = ObservationKind::Lldp;
    lobs.localIfname = "eth0";
    lobs.event = ObservationEvent::Present;
    lobs.observedAt = std::chrono::steady_clock::now();
    lobs.remoteChassisId = "aa:bb:cc:dd:ee:ff";
    engine.onLldpObservation(lobs);

    auto after = engine.findCandidateByMac("aa:bb:cc:dd:ee:ff");
    ASSERT_TRUE(after.has_value());
    EXPECT_TRUE(after->seenInLldp);
    EXPECT_EQ(after->remoteChassisId, "aa:bb:cc:dd:ee:ff");
}

TEST(LldpIngestionTest, PortIdFallback) {
    ModelConfig config;
    ObservationModelEngine engine(std::move(config));

    // Baseline neighbor to make candidate publishable
    NeighborObservation nobs;
    nobs.kind = ObservationKind::Neighbor;
    nobs.ifname = "eth0";
    nobs.event = ObservationEvent::Present;
    nobs.family = AF_INET;
    nobs.mac = "11:22:33:44:55:66";
    nobs.ip = "10.0.0.2";
    nobs.observedAt = std::chrono::steady_clock::now();
    engine.onNeighborObservation(nobs);
    engine.markLive();

    // LLDP with non-MAC chassisId but MAC-like portId
    LldpObservation lobs;
    lobs.kind = ObservationKind::Lldp;
    lobs.localIfname = "eth0";
    lobs.event = ObservationEvent::Present;
    lobs.observedAt = std::chrono::steady_clock::now();
    lobs.remoteChassisId = "hostname-abc";
    lobs.remotePortId = "11:22:33:44:55:66";
    engine.onLldpObservation(lobs);

    auto candidate = engine.findCandidateByMac("11:22:33:44:55:66");
    ASSERT_TRUE(candidate.has_value());
    EXPECT_TRUE(candidate->seenInLldp);
    EXPECT_EQ(candidate->remotePortId, "11:22:33:44:55:66");
}

TEST(LldpIngestionTest, NonMacIdentityIgnored) {
    ModelConfig config;
    ObservationModelEngine engine(std::move(config));

    LldpObservation obs;
    obs.kind = ObservationKind::Lldp;
    obs.localIfname = "eth0";
    obs.event = ObservationEvent::Present;
    obs.observedAt = std::chrono::steady_clock::now();
    obs.remoteChassisId = "hostname";
    obs.remotePortId = "eth0";

    engine.onLldpObservation(obs);

    // No candidate created — both chassisId and portId are non-MAC
    auto candidates = engine.remoteCandidates();
    EXPECT_TRUE(candidates.empty());
}

TEST(LldpIngestionTest, LldpRemovedClearsSeenInLldp) {
    ModelConfig config;
    ObservationModelEngine engine(std::move(config));

    // Publishable baseline: neighbor + LLDP
    NeighborObservation nobs;
    nobs.kind = ObservationKind::Neighbor;
    nobs.ifname = "eth0";
    nobs.event = ObservationEvent::Present;
    nobs.family = AF_INET;
    nobs.mac = "aa:bb:cc:dd:ee:ff";
    nobs.ip = "10.0.0.1";
    nobs.observedAt = std::chrono::steady_clock::now();
    engine.onNeighborObservation(nobs);

    LldpObservation add;
    add.kind = ObservationKind::Lldp;
    add.localIfname = "eth0";
    add.event = ObservationEvent::Present;
    add.observedAt = std::chrono::steady_clock::now();
    add.remoteChassisId = "aa:bb:cc:dd:ee:ff";
    engine.onLldpObservation(add);

    engine.markLive();

    auto before = engine.findCandidateByMac("aa:bb:cc:dd:ee:ff");
    ASSERT_TRUE(before.has_value());
    EXPECT_TRUE(before->seenInLldp);

    // Remove LLDP
    LldpObservation remove;
    remove.kind = ObservationKind::Lldp;
    remove.localIfname = "eth0";
    remove.event = ObservationEvent::Removed;
    remove.observedAt = std::chrono::steady_clock::now();
    remove.remoteChassisId = "aa:bb:cc:dd:ee:ff";
    engine.onLldpObservation(remove);

    // Candidate still exists due to neighbor evidence, but LLDP cleared
    auto after = engine.findCandidateByMac("aa:bb:cc:dd:ee:ff");
    ASSERT_TRUE(after.has_value());
    EXPECT_FALSE(after->seenInLldp);
}

TEST(LldpIngestionTest, AgedRestoredOnFreshLldpPresent) {
    ModelConfig config;
    config.candidateAgeout = std::chrono::seconds(1);
    ObservationModelEngine engine(std::move(config));

    // Baseline neighbor to make candidate publishable
    NeighborObservation nobs;
    nobs.kind = ObservationKind::Neighbor;
    nobs.ifname = "eth0";
    nobs.event = ObservationEvent::Present;
    nobs.family = AF_INET;
    nobs.mac = "aa:bb:cc:dd:ee:ff";
    nobs.ip = "10.0.0.1";
    nobs.observedAt = std::chrono::steady_clock::now();
    nobs.reachability = NeighborReachability::Reachable;
    engine.onNeighborObservation(nobs);
    engine.markLive();

    // Age to Aged
    auto future = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    engine.age(future);

    auto aged = engine.findCandidateByMac("aa:bb:cc:dd:ee:ff");
    ASSERT_TRUE(aged.has_value());
    EXPECT_EQ(aged->status, CandidateStatus::Aged);

    // Fresh LLDP evidence with uppercase MAC restores to Confirmed
    LldpObservation lobs;
    lobs.kind = ObservationKind::Lldp;
    lobs.localIfname = "eth0";
    lobs.event = ObservationEvent::Present;
    lobs.observedAt = std::chrono::steady_clock::now();
    lobs.remoteChassisId = "AA:BB:CC:DD:EE:FF";
    engine.onLldpObservation(lobs);

    auto restored = engine.findCandidateByMac("aa:bb:cc:dd:ee:ff");
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(restored->status, CandidateStatus::Confirmed);
    EXPECT_TRUE(restored->seenInLldp);
}

} // namespace
} // namespace RSCGroup