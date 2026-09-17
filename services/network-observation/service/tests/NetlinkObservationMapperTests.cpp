//
// Created by vvass on 18-Sep-26.
//

#include "NetlinkObservationMapper.h"

#include <linux/rtnetlink.h>
#include <sys/socket.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <utility>

namespace {
using namespace RSCGroup;
using namespace std::chrono_literals;

void expect(bool cond, const std::string &msg) {
    if (!cond) {
        std::cerr << "NetlinkObservationMapperTests: " << msg << '\n';
        std::exit(EXIT_FAILURE);
    }
}

/// Fixed, recognisable timestamp: proves observedAt is plumbed through rather
/// than re-sampled inside the mapping functions.
const std::chrono::steady_clock::time_point kStamp{std::chrono::duration_cast<std::chrono::steady_clock::duration>(42s)};

// ---------------------------------------------------------------------------
// Pure field mappings
// ---------------------------------------------------------------------------

void testNudToReachabilityCoversEveryKernelState() {
    using enum NeighborReachability;
    expect(nudToReachability(NUD_INCOMPLETE) == Incomplete, "NUD_INCOMPLETE");
    expect(nudToReachability(NUD_REACHABLE) == Reachable, "NUD_REACHABLE");
    expect(nudToReachability(NUD_STALE) == Stale, "NUD_STALE");
    expect(nudToReachability(NUD_DELAY) == Delay, "NUD_DELAY");
    expect(nudToReachability(NUD_PROBE) == Probe, "NUD_PROBE");
    expect(nudToReachability(NUD_FAILED) == Failed, "NUD_FAILED");
    expect(nudToReachability(NUD_NOARP) == NoArp, "NUD_NOARP");
    expect(nudToReachability(NUD_PERMANENT) == Permanent, "NUD_PERMANENT");
}

void testNudToReachabilityUnmappedStatesAreUnknown() {
    using enum NeighborReachability;
    // NUD_NONE is a real kernel state the switch deliberately does not map.
    expect(nudToReachability(NUD_NONE) == Unknown, "NUD_NONE maps to Unknown");
    expect(nudToReachability(0xFFFF) == Unknown, "garbage maps to Unknown");

    // NUD states are a bitmask: the kernel can report a combination, and the
    // switch matches exact values only. Anything combined is Unknown rather
    // than silently taking a branch.
    expect(nudToReachability(NUD_REACHABLE | NUD_PROBE) == Unknown,
           "combined states are not silently mapped to one branch");
}

void testFdbEntryKindPrecedence() {
    FdbEvent e;
    e.local = false;
    e.permanent = false;
    expect(fdbToEntryKind(e) == FdbEntryKind::Dynamic, "neither flag: Dynamic");

    e.permanent = true;
    expect(fdbToEntryKind(e) == FdbEntryKind::Static, "permanent only: Static");

    e.local = true;
    e.permanent = false;
    expect(fdbToEntryKind(e) == FdbEntryKind::Local, "local only: Local");

    // Precedence is load-bearing: a bridge-local entry is also permanent, so
    // `local` must be tested first or every local entry reports as Static.
    e.local = true;
    e.permanent = true;
    expect(fdbToEntryKind(e) == FdbEntryKind::Local, "local wins over permanent");
}

void testToObsEvent() {
    expect(toObsEvent(true) == ObservationEvent::Present, "true is Present");
    expect(toObsEvent(false) == ObservationEvent::Removed, "false is Removed");
}

void testMakeCidr() {
    InterfaceIpEvent e;
    e.address = "10.0.0.1";
    e.prefixLen = 24;
    expect(makeCidr(e) == "10.0.0.1/24", "IPv4 CIDR");

    e.address = "fe80::1";
    e.prefixLen = 64;
    expect(makeCidr(e) == "fe80::1/64", "IPv6 CIDR");

    // prefixLen is `unsigned char`: without the cast to int it would be
    // formatted as a character rather than a number.
    e.address = "10.0.0.1";
    e.prefixLen = 0;
    expect(makeCidr(e) == "10.0.0.1/0", "prefix 0 formats as a number, not a NUL");

    e.prefixLen = 128;
    expect(makeCidr(e) == "10.0.0.1/128", "prefix 128 formats as a number");
}

// ---------------------------------------------------------------------------
// Event → observation
// ---------------------------------------------------------------------------

void testToLinkObservationCarriesEveryField() {
    LinkEvent e;
    e.ifindex = 7;
    e.ifname = "eth0";
    e.mac = "aa:bb:cc:dd:ee:ff";
    e.adminUp = true;
    e.running = true;
    e.operState = 6; // IF_OPER_UP
    e.masterIfname = "br0";
    e.present = true;

    const auto o = toLinkObservation(e, kStamp);
    expect(o.kind == ObservationKind::Link, "kind");
    expect(o.observedAt == kStamp, "observedAt is the caller's stamp");
    expect(o.ifindex == 7, "ifindex");
    expect(o.ifname == "eth0", "ifname");
    expect(o.mac == "aa:bb:cc:dd:ee:ff", "mac");
    expect(o.adminUp && o.running, "adminUp/running");
    expect(o.operstate == "6", "operState is stringified");
    expect(o.masterIfname && *o.masterIfname == "br0", "masterIfname");
    expect(o.event == ObservationEvent::Present, "present maps to Present");
}

void testToLinkObservationAbsentMasterAndRemoval() {
    LinkEvent e;
    e.ifname = "eth1";
    e.present = false;

    const auto o = toLinkObservation(e, kStamp);
    expect(!o.masterIfname.has_value(), "absent master stays nullopt");
    expect(o.event == ObservationEvent::Removed, "RTM_DELLINK maps to Removed");
    expect(o.operstate == "0", "default operState stringifies to \"0\"");
}

void testToAddressObservation() {
    InterfaceIpEvent e;
    e.ifname = "eth0";
    e.family = AF_INET6;
    e.address = "fe80::1";
    e.prefixLen = 64;
    e.present = true;

    const auto o = toAddressObservation(e, kStamp);
    expect(o.kind == ObservationKind::Address, "kind");
    expect(o.observedAt == kStamp, "observedAt");
    expect(o.ifname == "eth0", "ifname");
    expect(o.family == AF_INET6, "family");
    expect(o.cidr == "fe80::1/64", "address and prefix are joined into cidr");
    expect(o.event == ObservationEvent::Present, "event");
}

void testToNeighborObservation() {
    NeighborEvent e;
    e.ifname = "eth0";
    e.mac = "aa:bb:cc:dd:ee:ff";
    e.family = AF_INET;
    e.ip = "10.0.0.2";
    e.nudState = NUD_STALE;
    e.present = true;

    const auto o = toNeighborObservation(e, kStamp);
    expect(o.kind == ObservationKind::Neighbor, "kind");
    expect(o.observedAt == kStamp, "observedAt");
    expect(o.ifname == "eth0", "ifname");
    expect(o.mac == "aa:bb:cc:dd:ee:ff", "mac");
    expect(o.family == AF_INET, "family");
    expect(o.ip == "10.0.0.2", "ip");
    expect(o.reachability == NeighborReachability::Stale, "nudState is translated");
    expect(o.event == ObservationEvent::Present, "event");
}

void testToFdbObservationRenamesIfnameToPortIfname() {
    FdbEvent e;
    e.ifname = "swp1";
    e.mac = "11:22:33:44:55:66";
    e.local = false;
    e.permanent = true;
    e.present = false;

    const auto o = toFdbObservation(e, kStamp);
    expect(o.kind == ObservationKind::Fdb, "kind");
    expect(o.observedAt == kStamp, "observedAt");
    // FdbEvent::ifname becomes FdbObservation::portIfname — the one field
    // rename in the mapper, and the easiest to get wrong.
    expect(o.portIfname == "swp1", "ifname maps to portIfname");
    expect(o.mac == "11:22:33:44:55:66", "mac");
    expect(o.entryKind == FdbEntryKind::Static, "entryKind is derived");
    expect(o.event == ObservationEvent::Removed, "event");
}

// ---------------------------------------------------------------------------
// makeCallbacks fan-out
// ---------------------------------------------------------------------------

struct Recorder {
    std::vector<LinkObservation> links;
    std::vector<AddressObservation> addresses;
    std::vector<NeighborObservation> neighbors;
    std::vector<FdbObservation> fdb;
    std::vector<std::pair<std::string, bool> > lldpLinkState;
    std::vector<std::string> order;

    [[nodiscard]] ObservationSinks sinks() {
        ObservationSinks s;
        s.onLink = [this](LinkObservation o) {
            links.push_back(std::move(o));
            order.emplace_back("link");
        };
        s.onAddress = [this](AddressObservation o) { addresses.push_back(std::move(o)); };
        s.onNeighbor = [this](NeighborObservation o) { neighbors.push_back(std::move(o)); };
        s.onFdb = [this](FdbObservation o) { fdb.push_back(std::move(o)); };
        s.onLinkStateForLldp = [this](const std::string &ifname, bool up) {
            lldpLinkState.emplace_back(ifname, up);
            order.emplace_back("lldp");
        };
        return s;
    }
};

void testEachCallbackRoutesToItsOwnSink() {
    Recorder r;
    auto cb = makeCallbacks(r.sinks());

    LinkEvent link;
    link.ifname = "eth0";
    link.present = true;
    link.running = true;
    cb.onLinkChanged(link);

    InterfaceIpEvent addr;
    addr.ifname = "eth0";
    addr.address = "10.0.0.1";
    addr.prefixLen = 24;
    addr.present = true;
    cb.onInterfaceIpChanged(addr);

    NeighborEvent neigh;
    neigh.ifname = "eth0";
    neigh.mac = "aa:bb:cc:dd:ee:ff";
    neigh.nudState = NUD_REACHABLE;
    neigh.present = true;
    cb.onNeighborChanged(neigh);

    FdbEvent fdb;
    fdb.ifname = "swp1";
    fdb.mac = "11:22:33:44:55:66";
    fdb.present = true;
    cb.onFdbChanged(fdb);

    expect(r.links.size() == 1 && r.links[0].ifname == "eth0", "link routed");
    expect(r.addresses.size() == 1 && r.addresses[0].cidr == "10.0.0.1/24", "address routed");
    expect(r.neighbors.size() == 1 &&
           r.neighbors[0].reachability == NeighborReachability::Reachable,
           "neighbor routed and translated");
    expect(r.fdb.size() == 1 && r.fdb[0].portIfname == "swp1", "fdb routed");
}

void testLinkStateForLldpTruthTable() {
    // `up` is present && running. Every other combination is down, because a
    // removed or carrier-less interface must flush its LLDP neighbours.
    const struct {
        bool present, running, expectUp;
        const char *desc;
    } cases[] = {
        {true, true, true, "present + running is up"},
        {true, false, false, "present but no carrier is down"},
        {false, true, false, "absent is down even if running was set"},
        {false, false, false, "absent and not running is down"},
    };

    for (const auto &c: cases) {
        Recorder r;
        auto cb = makeCallbacks(r.sinks());
        LinkEvent e;
        e.ifname = "eth0";
        e.present = c.present;
        e.running = c.running;
        cb.onLinkChanged(e);

        expect(r.lldpLinkState.size() == 1, "lldp sink invoked once per link event");
        expect(r.lldpLinkState[0].first == "eth0", "ifname forwarded");
        expect(r.lldpLinkState[0].second == c.expectUp, c.desc);
    }
}

void testLinkSinkRunsBeforeLldpSink() {
    // The model sees the link change before the LLDP source is told to flush
    // or refresh that interface. Reversing this would let an LLDP removal be
    // processed against a model that still believes the link is up.
    Recorder r;
    auto cb = makeCallbacks(r.sinks());

    LinkEvent e;
    e.ifname = "eth0";
    e.present = true;
    e.running = false;
    cb.onLinkChanged(e);

    expect((r.order == std::vector<std::string>{"link", "lldp"}),
           "onLink runs before onLinkStateForLldp");
}

void testEmptySinksAreSkipped() {
    // A caller may wire only the events it needs; unset sinks must not be
    // invoked through an empty std::function.
    auto cb = makeCallbacks(ObservationSinks{});

    LinkEvent link;
    link.ifname = "eth0";
    link.present = true;
    link.running = true;

    InterfaceIpEvent addr;
    addr.ifname = "eth0";
    addr.address = "10.0.0.1";

    NeighborEvent neigh;
    neigh.ifname = "eth0";

    FdbEvent fdb;
    fdb.ifname = "swp1";

    // No crash, no bad_function_call.
    cb.onLinkChanged(link);
    cb.onInterfaceIpChanged(addr);
    cb.onNeighborChanged(neigh);
    cb.onFdbChanged(fdb);
}

void testPartialSinksAreIndependent() {
    // Specifically: an absent onLink must not suppress onLinkStateForLldp,
    // and vice versa — they are separate branches inside one callback.
    std::vector<std::pair<std::string, bool> > lldpOnly;
    ObservationSinks s;
    s.onLinkStateForLldp = [&](const std::string &ifname, bool up) {
        lldpOnly.emplace_back(ifname, up);
    };
    auto cb = makeCallbacks(std::move(s));

    LinkEvent e;
    e.ifname = "eth0";
    e.present = true;
    e.running = true;
    cb.onLinkChanged(e);

    expect(lldpOnly.size() == 1 && lldpOnly[0].second,
           "lldp sink fires even with no onLink sink");
}

void testCallbacksOwnTheirSinksAfterTheFactoryReturns() {
    // makeCallbacks takes ObservationSinks by value and the lambdas capture a
    // copy, so the callbacks stay valid after the caller's sinks object dies.
    // The runtime relies on this: it builds sinks as a local and hands the
    // resulting MonitorCallbacks to NetlinkNetworkMonitor.
    std::vector<std::string> seen;
    MonitorCallbacks cb;
    {
        ObservationSinks s;
        s.onLink = [&seen](LinkObservation o) { seen.push_back(o.ifname); };
        cb = makeCallbacks(std::move(s));
    } // s is gone

    LinkEvent e;
    e.ifname = "eth0";
    e.present = true;
    cb.onLinkChanged(e);

    expect(seen == std::vector<std::string>{"eth0"},
           "callbacks remain valid after the sinks object is destroyed");
}

void testObservedAtIsSampledPerEvent() {
    // makeCallbacks samples steady_clock at dispatch time, unlike the to*
    // functions which take the stamp. Assert it is populated and monotonic
    // rather than left at the epoch.
    Recorder r;
    auto cb = makeCallbacks(r.sinks());

    const auto before = std::chrono::steady_clock::now();
    LinkEvent e;
    e.ifname = "eth0";
    e.present = true;
    cb.onLinkChanged(e);
    const auto after = std::chrono::steady_clock::now();

    expect(r.links.size() == 1, "one link observation");
    expect(r.links[0].observedAt >= before && r.links[0].observedAt <= after,
           "observedAt is sampled at dispatch");
}
} // namespace

int main() {
    // Pure field mappings
    testNudToReachabilityCoversEveryKernelState();
    testNudToReachabilityUnmappedStatesAreUnknown();
    testFdbEntryKindPrecedence();
    testToObsEvent();
    testMakeCidr();

    // Event → observation
    testToLinkObservationCarriesEveryField();
    testToLinkObservationAbsentMasterAndRemoval();
    testToAddressObservation();
    testToNeighborObservation();
    testToFdbObservationRenamesIfnameToPortIfname();

    // makeCallbacks fan-out
    testEachCallbackRoutesToItsOwnSink();
    testLinkStateForLldpTruthTable();
    testLinkSinkRunsBeforeLldpSink();
    testEmptySinksAreSkipped();
    testPartialSinksAreIndependent();
    testCallbacksOwnTheirSinksAfterTheFactoryReturns();
    testObservedAtIsSampledPerEvent();

    return EXIT_SUCCESS;
}