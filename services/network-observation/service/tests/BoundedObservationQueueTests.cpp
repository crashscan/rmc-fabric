//
// Created by vvass on 17-Sep-26.
//

#include "BoundedObservationQueue.h"

#include <sys/socket.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using namespace RSCGroup;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "BoundedObservationQueueTests: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

QueuedItem makeItem(ObservationSource source, int id)
{
    NeighborObservation obs;
    obs.kind = ObservationKind::Neighbor;
    obs.ifname = "eth0";
    obs.event = ObservationEvent::Present;
    obs.family = AF_INET;
    obs.mac = "aa:bb:cc:dd:ee:ff";
    obs.ip = std::to_string(id);
    obs.observedAt = std::chrono::steady_clock::now();
    return QueuedItem{source, obs};
}

int idOf(const QueuedItem& item)
{
    return std::stoi(std::get<NeighborObservation>(item.payload).ip);
}

void testPayloadAndSourceSurviveRoundTrip()
{
    BoundedObservationQueue q(2);
    q.push(makeItem(ObservationSource::Lldp, 42));

    const auto item = q.tryPop();
    expect(item && item->source == ObservationSource::Lldp && idOf(*item) == 42,
           "payload and source survive the round trip");
}

void testResyncBitFromDiscardedSourceNotAccepted()
{
    BoundedObservationQueue q(2);
    q.push(makeItem(ObservationSource::Lldp, 1));
    q.push(makeItem(ObservationSource::Netlink, 2));   // full

    expect(!q.push(makeItem(ObservationSource::Netlink, 3)),
           "overflow push reports the drop");
    expect(q.resyncMask() == sourceBit(ObservationSource::Lldp),
           "resync bit is raised for the DISCARDED item's source");
    expect((q.resyncMask() & sourceBit(ObservationSource::Netlink)) == 0u,
           "the accepted item's source must not be marked for resync");
}

void testMaskAccumulatesAcrossSourcesAndTakeClears()
{
    BoundedObservationQueue q(2);
    q.push(makeItem(ObservationSource::Netlink, 1));
    q.push(makeItem(ObservationSource::Netlink, 2));   // full
    q.push(makeItem(ObservationSource::Lldp, 3));      // drop N1 -> Netlink bit
    q.push(makeItem(ObservationSource::Lldp, 4));      // drop N2 -> Netlink bit
    q.push(makeItem(ObservationSource::Netlink, 5));   // drop L3 -> Lldp bit

    const auto both = sourceBit(ObservationSource::Netlink) | sourceBit(ObservationSource::Lldp);
    expect(q.resyncMask() == both, "drops across sources accumulate via OR");
    expect(q.takeResyncMask() == both, "take returns the accumulated union");
    expect(q.takeResyncMask() == 0u, "take clears the mask");

    const auto a = q.tryPop();
    const auto b = q.tryPop();
    expect(a && idOf(*a) == 4 && b && idOf(*b) == 5, "surviving items drain in order");
}

void testCloseDrainsAndIgnoredPushesRaiseNoBit()
{
    BoundedObservationQueue q(2);
    q.push(makeItem(ObservationSource::Netlink, 1));
    q.close();

    expect(q.push(makeItem(ObservationSource::Netlink, 2)),
           "push after close is silently ignored");
    expect(q.resyncMask() == 0u, "ignored pushes raise no resync bit");

    const auto item = q.waitPop();
    expect(item && idOf(*item) == 1, "close drains already-queued items");
    expect(!q.waitPop().has_value(), "then exhaustion");
}

void testStatsPassThrough()
{
    BoundedObservationQueue q(2);
    expect(q.stats().capacity == 2 && !q.stats().closed, "stats expose capacity/state");

    q.push(makeItem(ObservationSource::Netlink, 1));
    q.push(makeItem(ObservationSource::Netlink, 2));
    q.push(makeItem(ObservationSource::Netlink, 3));   // one drop

    const auto st = q.stats();
    expect(st.dropped == 1 && st.highWater == 2 && st.size == 2,
           "drop accounting and watermarks pass through");
}

} // namespace

int main()
{
    testPayloadAndSourceSurviveRoundTrip();
    testResyncBitFromDiscardedSourceNotAccepted();
    testMaskAccumulatesAcrossSourcesAndTakeClears();
    testCloseDrainsAndIgnoredPushesRaiseNoBit();
    testStatsPassThrough();
    return EXIT_SUCCESS;
}
