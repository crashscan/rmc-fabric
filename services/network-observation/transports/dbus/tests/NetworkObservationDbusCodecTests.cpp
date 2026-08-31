#include "NetworkObservationDbusCodec.h"

#include <DecodeError.hpp>

#include <cstdlib>
#include <dbus-cxx/variant.h>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

namespace codec = RSCGroup::NetworkObservationDbusCodec;
namespace contract = interop_contract::network_observation;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void testLocalInterfaceRoundTrip()
{
    contract::LocalInterfaceState in;
    in.ifindex = 42;
    in.ifname = "eth0";
    in.mac = "aa:bb:cc:dd:ee:ff";
    in.adminUp = true;
    in.running = true;
    in.operstate = "UP";
    in.masterIfname = "br0";
    in.ipv4 = {"10.0.0.1/24"};
    in.ipv6 = {"fe80::1/64"};

    const auto encoded = codec::toVariantMap(in);
    const auto out = codec::fromVariantMapIface(encoded);

    expect(out.ifindex == in.ifindex, "ifindex round-trip mismatch");
    expect(out.ifname == in.ifname, "ifname round-trip mismatch");
    expect(out.mac == in.mac, "mac round-trip mismatch");
    expect(out.adminUp == in.adminUp, "adminUp round-trip mismatch");
    expect(out.running == in.running, "running round-trip mismatch");
    expect(out.operstate == in.operstate, "operstate round-trip mismatch");
    expect(out.masterIfname == in.masterIfname, "master round-trip mismatch");
    expect(out.ipv4 == in.ipv4, "ipv4 round-trip mismatch");
    expect(out.ipv6 == in.ipv6, "ipv6 round-trip mismatch");
}

void testRemoteCandidateRoundTrip()
{
    contract::RemoteCandidate in;
    in.mac = "00:11:22:33:44:55";
    in.classification = contract::CandidateClassification::RemoteEndpoint;
    in.status = contract::CandidateStatus::Confirmed;
    in.seenInFdb = true;
    in.seenInNeigh = true;
    in.seenInLldp = true;
    in.bridgePort = "swp1";
    in.remoteChassisId = "chassis-id";
    in.remotePortId = "port-id";
    in.remoteSystemName = "remote-node";
    in.neighborIfaces = {"eth0"};
    in.ipv4 = {"10.0.0.2/24"};
    in.ipv6 = {"fe80::2/64"};

    const auto encoded = codec::toVariantMap(in);
    const auto out = codec::fromVariantMapCandidate(encoded);

    expect(out.mac == in.mac, "candidate mac round-trip mismatch");
    expect(out.classification == in.classification, "classification round-trip mismatch");
    expect(out.status == in.status, "status round-trip mismatch");
    expect(out.seenInFdb == in.seenInFdb, "seenInFdb round-trip mismatch");
    expect(out.seenInNeigh == in.seenInNeigh, "seenInNeigh round-trip mismatch");
    expect(out.seenInLldp == in.seenInLldp, "seenInLldp round-trip mismatch");
    expect(out.bridgePort == in.bridgePort, "bridgePort round-trip mismatch");
    expect(out.remoteChassisId == in.remoteChassisId, "remoteChassisId round-trip mismatch");
    expect(out.remotePortId == in.remotePortId, "remotePortId round-trip mismatch");
    expect(out.remoteSystemName == in.remoteSystemName, "remoteSystemName round-trip mismatch");
    expect(out.neighborIfaces == in.neighborIfaces, "neighborIfaces round-trip mismatch");
    expect(out.ipv4 == in.ipv4, "candidate ipv4 round-trip mismatch");
    expect(out.ipv6 == in.ipv6, "candidate ipv6 round-trip mismatch");
}

void testLocalInterfaceRejectsMissingRequiredField()
{
    std::map<std::string, DBus::Variant> raw;
    raw[std::string(contract::K_IFINDEX)] = DBus::Variant(int32_t{7});
    raw[std::string(contract::K_MAC)] = DBus::Variant(std::string("aa:bb:cc:dd:ee:ff"));
    raw[std::string(contract::K_ADMINUP)] = DBus::Variant(true);
    raw[std::string(contract::K_RUNNING)] = DBus::Variant(true);
    raw[std::string(contract::K_OPERSTATE)] = DBus::Variant(std::string("UP"));
    raw[std::string(contract::K_IPV4)] = DBus::Variant(std::vector<DBus::Variant>{});
    raw[std::string(contract::K_IPV6)] = DBus::Variant(std::vector<DBus::Variant>{});

    bool threw = false;
    try {
        (void)codec::fromVariantMapIface(raw);
    } catch (const interop_contract::DecodeError&) {
        threw = true;
    }
    expect(threw, "fromVariantMapIface should reject missing ifname");
}

void testRemoteCandidateRejectsUnknownStatus()
{
    std::map<std::string, DBus::Variant> raw;
    raw[std::string(contract::K_MAC)] = DBus::Variant(std::string("00:11:22:33:44:55"));
    raw[std::string(contract::K_CLASSIFICATION)] = DBus::Variant(std::string("RemoteEndpoint"));
    raw[std::string(contract::K_STATUS)] = DBus::Variant(std::string("Broken"));
    raw[std::string(contract::K_SEEN_IN_FDB)] = DBus::Variant(true);
    raw[std::string(contract::K_SEEN_IN_NEIGH)] = DBus::Variant(false);
    raw[std::string(contract::K_SEEN_IN_LLDP)] = DBus::Variant(true);
    raw[std::string(contract::K_NEIGHBOR_IFACES)] = DBus::Variant(std::vector<DBus::Variant>{});
    raw[std::string(contract::K_IPV4)] = DBus::Variant(std::vector<DBus::Variant>{});
    raw[std::string(contract::K_IPV6)] = DBus::Variant(std::vector<DBus::Variant>{});

    bool threw = false;
    try {
        (void)codec::fromVariantMapCandidate(raw);
    } catch (const interop_contract::DecodeError&) {
        threw = true;
    }
    expect(threw, "fromVariantMapCandidate should reject unknown status");
}

} // namespace

int main()
{
    testLocalInterfaceRoundTrip();
    testRemoteCandidateRoundTrip();
    testLocalInterfaceRejectsMissingRequiredField();
    testRemoteCandidateRejectsUnknownStatus();
    return EXIT_SUCCESS;
}
