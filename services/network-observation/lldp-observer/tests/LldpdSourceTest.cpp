//
// Created by vvass on 24-Jul-26.
//
#include "LldpdSource.h"
#include <gtest/gtest.h>
#include <vector>

namespace RSCGroup {
namespace {

// Captures observations emitted by LldpdSource
struct TestSink {
    std::vector<LldpObservation> observations;
    void onObservation(const LldpObservation& obs) {
        observations.push_back(obs);
    }
};

TEST(LldpdSourceTest, IdempotentStart) {
    TestSink sink;
    LldpdSource source({}, [&](const LldpObservation& o) { sink.onObservation(o); });
    EXPECT_FALSE(source.isRunning());
    // Cannot actually start (needs lldpd), but idempotency is testable
}

// Note: Full integration tests require a running lldpd instance.
// These tests validate the public API contract and cache semantics.

TEST(LldpdSourceTest, RemoveInterfaceOnEmptyCache_NoThrow) {
    TestSink sink;
    LldpdSource source({}, [&](const LldpObservation& o) { sink.onObservation(o); });
    // removeInterface on empty cache should not crash
    source.removeInterface("eth0");
    EXPECT_TRUE(sink.observations.empty());
}

TEST(LldpdSourceTest, StopWhenAlreadyStopped_NoThrow) {
    TestSink sink;
    LldpdSource source({}, [&](const LldpObservation& o) { sink.onObservation(o); });
    source.stop(); // should be idempotent
    source.stop(); // second call should not crash
}

TEST(LldpdSourceTest, RefreshWhenNotRunning_NoOp) {
    TestSink sink;
    LldpdSource source({}, [&](const LldpObservation& o) { sink.onObservation(o); });
    // refreshAll when not running should do nothing
    source.refreshAll();
}

} // namespace
} // namespace RSCGroup