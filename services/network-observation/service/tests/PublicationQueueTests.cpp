//
// Created by vvass on 18-Sep-26.
//
#include "PublicationQueue.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

using namespace RSCGroup;
using namespace std::chrono_literals;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "PublicationQueueTests: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

// Ten thousand marks of one key occupy exactly one entry.
void testCoalescingBoundsSizeByCardinality()
{
    PublicationQueue q;
    for (int i = 0; i < 10000; ++i) {
        expect(q.markCandidateChanged("aa:bb:cc:dd:ee:ff"), "mark accepted");
    }
    expect(q.size() == 1, "rapid updates to one key occupy one entry");
    expect(q.stats().coalesced == 9999, "repeated marks count as coalescing");

    const auto pending = q.waitAndTake();
    expect(pending && pending->size() == 1, "one entry survives the take");
    expect(pending->at(CandidateKey{"aa:bb:cc:dd:ee:ff"}) == PublicationIntent::Changed,
           "latest intent survives");
}

void testRemovedSupersedesPendingChanged()
{
    PublicationQueue q;
    q.markCandidateChanged("m1");
    q.markCandidateRemoved("m1");
    const auto pending = q.tryTake();
    expect(pending && pending->at(CandidateKey{"m1"}) == PublicationIntent::Removed,
           "Removed supersedes a pending Changed");
}

void testRevivalChangedSupersedesPendingRemoved()
{
    PublicationQueue q;
    q.markCandidateRemoved("m1");
    q.markCandidateChanged("m1");
    const auto pending = q.tryTake();
    expect(pending && pending->at(CandidateKey{"m1"}) == PublicationIntent::Changed,
           "revival Changed supersedes a pending Removed");
}

void testChannelsAndUnitKeyAreIndependent()
{
    PublicationQueue q;
    q.markCandidateChanged("m1");
    q.markInterfaceChanged("eth0");
    q.markInterfaceRemoved("eth1");
    q.markLocalStateChanged();
    q.markLocalStateChanged();
    q.markLocalStateChanged();

    const auto pending = q.tryTake();
    expect(pending && pending->size() == 4, "channels accumulate independently");
    expect(pending->count(LocalStateKey{}) == 1,
           "local state flag collapses to a single entry");
}

void testTakeSwapsGenerations()
{
    PublicationQueue q;
    q.markCandidateChanged("m1");
    const auto first = q.tryTake();
    expect(first && first->size() == 1, "first generation taken");
    expect(!q.tryTake().has_value(), "set is empty after take");

    q.markInterfaceChanged("eth0");
    const auto second = q.tryTake();
    expect(second && second->size() == 1 && second->count(InterfaceKey{"eth0"}) == 1,
           "re-marks after take land in the next generation");
}

void testWaitAndTakeBlocksUntilMark()
{
    PublicationQueue q;
    std::atomic<bool> returned{false};
    std::optional<PendingPublication> out;
    std::thread consumer([&] { out = q.waitAndTake(); returned = true; });

    std::this_thread::sleep_for(50ms);
    expect(!returned.load(), "waitAndTake blocks while empty");

    q.markLocalStateChanged();
    consumer.join();
    expect(returned.load() && out && out->count(LocalStateKey{}) == 1,
           "waitAndTake wakes with the marked generation");
}

void testCloseDrainsThenExhausts()
{
    PublicationQueue q;
    q.markCandidateChanged("m1");
    q.markInterfaceRemoved("eth0");
    q.close();
    q.close();   // idempotent

    expect(!q.markCandidateChanged("m2"), "mark after close is rejected");
    expect(q.size() == 2, "rejected mark does not enqueue");

    const auto pending = q.waitAndTake();
    expect(pending && pending->size() == 2, "close drains the pending generation");
    expect(!q.waitAndTake().has_value(), "then exhaustion: nullopt once closed and empty");
}

void testHighWaterTracksPeakKeys()
{
    PublicationQueue q;
    expect(q.stats().highWater == 0, "high water starts at zero");
    q.markCandidateChanged("m1");
    q.markCandidateChanged("m2");
    expect(q.stats().highWater == 2, "high water tracks distinct keys");
    q.markCandidateChanged("m1");       // coalesce, size unchanged
    expect(q.stats().highWater == 2, "coalescing does not raise the watermark");
    q.tryTake();
    expect(q.stats().highWater == 2, "watermark survives the take");
}

} // namespace

int main()
{
    testCoalescingBoundsSizeByCardinality();
    testRemovedSupersedesPendingChanged();
    testRevivalChangedSupersedesPendingRemoved();
    testChannelsAndUnitKeyAreIndependent();
    testTakeSwapsGenerations();
    testWaitAndTakeBlocksUntilMark();
    testCloseDrainsThenExhausts();
    testHighWaterTracksPeakKeys();
    return EXIT_SUCCESS;
}
