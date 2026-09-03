#include "NetworkObservationDbusCodec.h"

#include <DecodeError.hpp>
#include <IngressLimits.hpp>
#include <network_observation/NetworkObservationContracts.hpp>

#include <cstdlib>
#include <dbus-cxx/variant.h>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <utility>

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

// Build a complete, valid issue fields variant map.
[[nodiscard]] std::map<std::string, DBus::Variant> makeValidIssueFields()
{
    return {
        {std::string(contract::ISSUE_SEVERITY),  DBus::Variant(std::string("warning"))},
        {std::string(contract::ISSUE_MESSAGE),   DBus::Variant(std::string("something failed"))},
        {std::string(contract::ISSUE_COMPONENT), DBus::Variant(std::string("transport.dbus"))},
        {std::string(contract::ISSUE_OPERATION), DBus::Variant(std::string("publish"))},
        {std::string(contract::ISSUE_CATEGORY),  DBus::Variant(std::string("transport_publish_failed"))},
        {std::string(contract::ISSUE_IDENTITY),  DBus::Variant(std::string("dbus"))},
    };
}

void testDecodeIssuesValidRoundTrip()
{
    std::map<std::string, std::map<std::string, DBus::Variant>> encoded;
    encoded["observation.transport.dbus.publish.failed"] = makeValidIssueFields();

    const auto decoded = codec::decodeIssues(encoded);
    expect(decoded.size() == 1, "decoded issues count mismatch");
    expect(decoded.count("observation.transport.dbus.publish.failed") == 1,
           "decoded issue key missing");
    const auto& fields = decoded.at("observation.transport.dbus.publish.failed");
    expect(fields.at(std::string(contract::ISSUE_SEVERITY)) == "warning",
           "decoded severity mismatch");
}

void testDecodeIssuesRejectsOversizedOuterMap()
{
    std::map<std::string, std::map<std::string, DBus::Variant>> encoded;
    const auto limit = interop_contract::ingress::network_observation::kMaxIssues;
    for (std::size_t i = 0; i <= limit; ++i) {
        encoded["issue." + std::to_string(i)] = makeValidIssueFields();
    }

    bool threw = false;
    try {
        (void)codec::decodeIssues(encoded);
    } catch (const interop_contract::DecodeError& e) {
        threw = e.code() == interop_contract::DecodeErrorCode::limit_exceeded;
    }
    expect(threw, "decodeIssues should reject outer map exceeding kMaxIssues");
}

void testDecodeIssuesRejectsOversizedInnerMap()
{
    std::map<std::string, DBus::Variant> bigFields = makeValidIssueFields();
    const auto limit = interop_contract::ingress::network_observation::kMaxIssueFields;
    for (std::size_t i = bigFields.size(); i <= limit; ++i) {
        bigFields["extra_" + std::to_string(i)] = DBus::Variant(std::string("x"));
    }

    std::map<std::string, std::map<std::string, DBus::Variant>> encoded;
    encoded["some.issue"] = bigFields;

    bool threw = false;
    try {
        (void)codec::decodeIssues(encoded);
    } catch (const interop_contract::DecodeError& e) {
        threw = e.code() == interop_contract::DecodeErrorCode::limit_exceeded;
    }
    expect(threw, "decodeIssues should reject inner fields map exceeding kMaxIssueFields");
}

void testDecodeIssuesRejectsOversizedKey()
{
    const std::string longKey(interop_contract::ingress::kMaxKeyLength + 1, 'a');
    std::map<std::string, std::map<std::string, DBus::Variant>> encoded;
    encoded[longKey] = makeValidIssueFields();

    bool threw = false;
    try {
        (void)codec::decodeIssues(encoded);
    } catch (const interop_contract::DecodeError& e) {
        threw = e.code() == interop_contract::DecodeErrorCode::limit_exceeded;
    }
    expect(threw, "decodeIssues should reject oversized issue code key");
}

void testDecodeIssuesRejectsMissingRequiredField()
{
    auto fields = makeValidIssueFields();
    fields.erase(std::string(contract::ISSUE_SEVERITY));

    std::map<std::string, std::map<std::string, DBus::Variant>> encoded;
    encoded["some.issue"] = fields;

    bool threw = false;
    try {
        (void)codec::decodeIssues(encoded);
    } catch (const interop_contract::DecodeError& e) {
        threw = e.code() == interop_contract::DecodeErrorCode::missing_required_field;
    }
    expect(threw, "decodeIssues should reject issue missing required 'severity' field");
}

void testDecodeIssuesRejectsWrongFieldType()
{
    auto fields = makeValidIssueFields();
    // Replace string "severity" with a non-string variant.
    fields[std::string(contract::ISSUE_SEVERITY)] = DBus::Variant(int32_t{1});

    std::map<std::string, std::map<std::string, DBus::Variant>> encoded;
    encoded["some.issue"] = fields;

    bool threw = false;
    try {
        (void)codec::decodeIssues(encoded);
    } catch (const interop_contract::DecodeError& e) {
        threw = e.code() == interop_contract::DecodeErrorCode::invalid_type;
    }
    expect(threw, "decodeIssues should reject non-string issue field value");
}

template<typename Function>
void expectLimitExceeded(Function&& function, const std::string& message)
{
    try {
        std::forward<Function>(function)();
    } catch (const interop_contract::DecodeError& error) {
        expect(error.code() == interop_contract::DecodeErrorCode::limit_exceeded, message + ": unexpected error code");
        return;
    }

    expect(false, message + ": expected limit-exceeded error");
}

void testEncodeLocalSnapshotAcceptsBoundedSnapshot()
{
    contract::LocalInterfaceState interface;
    interface.ifindex = 7;
    interface.ifname = "eth0";
    interface.mac = "aa:bb:cc:dd:ee:ff";
    interface.operstate = "UP";

    contract::LocalNetworkSnapshot snapshot;
    snapshot.interfaces.emplace(interface.ifname, interface);

    const auto encoded = codec::encodeLocalSnapshot(snapshot);
    expect(encoded.size() == 1, "bounded local snapshot should encode");
    expect(encoded.contains("eth0"), "encoded local snapshot should contain eth0");
}

void testEncodeLocalSnapshotRejectsTooManyInterfaces()
{
    contract::LocalNetworkSnapshot snapshot;
    for (std::size_t index = 0; index <= interop_contract::ingress::network_observation::kMaxInterfaces; ++index) {
        contract::LocalInterfaceState interface;
        interface.ifindex = static_cast<int>(index);
        interface.ifname = "eth" + std::to_string(index);
        interface.mac = "aa:bb:cc:dd:ee:ff";
        interface.operstate = "UP";
        snapshot.interfaces.emplace(interface.ifname, std::move(interface));
    }

    expectLimitExceeded([&] { (void)codec::encodeLocalSnapshot(snapshot); }, "oversized local snapshot must be rejected");
}

void testEncodeLocalSnapshotRejectsOversizedKey()
{
    contract::LocalInterfaceState interface;
    interface.ifindex = 7;
    interface.ifname = "eth0";
    interface.mac = "aa:bb:cc:dd:ee:ff";
    interface.operstate = "UP";

    contract::LocalNetworkSnapshot snapshot;
    snapshot.interfaces.emplace(std::string(interop_contract::ingress::kMaxKeyLength + 1, 'x'), std::move(interface));

    expectLimitExceeded([&] { (void)codec::encodeLocalSnapshot(snapshot); }, "oversized snapshot key must be rejected");
}

void testEncodeCandidateMacsAcceptsBoundedList()
{
    const std::vector<std::string> macs{"00:11:22:33:44:55", "66:77:88:99:aa:bb"};
    expect(codec::encodeCandidateMacs(macs) == macs, "bounded candidate MAC list should encode unchanged");
}

void testEncodeCandidateMacsRejectsTooManyEntries()
{
    std::vector<std::string> macs(interop_contract::ingress::network_observation::kMaxCandidates + 1, "00:11:22:33:44:55");
    expectLimitExceeded([&] { (void)codec::encodeCandidateMacs(macs); }, "oversized candidate MAC list must be rejected");
}

void testEncodeCandidateMacsRejectsOversizedEntry()
{
    const std::vector<std::string> macs{std::string(interop_contract::ingress::kMaxStringLength + 1, 'x')};
    expectLimitExceeded([&] { (void)codec::encodeCandidateMacs(macs); }, "oversized candidate MAC must be rejected");
}

void testEncodeIssuesRejectsMissingRequiredField()
{
    contract::ObservationIssueFields fields{
            {std::string(contract::ISSUE_MESSAGE), "failure"},
            {std::string(contract::ISSUE_COMPONENT), "transport.dbus"},
            {std::string(contract::ISSUE_OPERATION), "publish"},
            {std::string(contract::ISSUE_CATEGORY), "transport_publish_failed"},
            {std::string(contract::ISSUE_IDENTITY), "dbus"},
        };

    contract::ObservationIssues issues;
    issues.emplace("observation.transport.failure", std::move(fields));

    bool threw = false;
    try {
        (void)codec::encodeIssues(issues);
    } catch (const interop_contract::DecodeError& error) {
        threw = error.code() ==
            interop_contract::DecodeErrorCode::missing_required_field;
    }

    expect(threw, "encodeIssues should reject a missing required field");
}

void testEncodeLocalSnapshotRejectsMismatchedInterfaceName()
{
    contract::LocalInterfaceState interface;
    interface.ifindex = 7;
    interface.ifname = "eth1";
    interface.mac = "aa:bb:cc:dd:ee:ff";
    interface.operstate = "UP";

    contract::LocalNetworkSnapshot snapshot;
    snapshot.interfaces.emplace("eth0", std::move(interface));

    bool threw = false;
    try {
        (void)codec::encodeLocalSnapshot(snapshot);
    } catch (const interop_contract::DecodeError& error) {
        threw = error.code() == interop_contract::DecodeErrorCode::invalid_value;
    }

    expect(threw, "encodeLocalSnapshot should reject a key/ifname mismatch");
}

} // namespace

int main()
{
    testLocalInterfaceRoundTrip();
    testRemoteCandidateRoundTrip();
    testLocalInterfaceRejectsMissingRequiredField();
    testRemoteCandidateRejectsUnknownStatus();
    testDecodeIssuesValidRoundTrip();
    testDecodeIssuesRejectsOversizedOuterMap();
    testDecodeIssuesRejectsOversizedInnerMap();
    testDecodeIssuesRejectsOversizedKey();
    testDecodeIssuesRejectsMissingRequiredField();
    testEncodeLocalSnapshotAcceptsBoundedSnapshot();
    testEncodeLocalSnapshotRejectsTooManyInterfaces();
    testEncodeLocalSnapshotRejectsOversizedKey();
    testEncodeCandidateMacsAcceptsBoundedList();
    testEncodeCandidateMacsRejectsTooManyEntries();
    testEncodeCandidateMacsRejectsOversizedEntry();
    testDecodeIssuesRejectsWrongFieldType();
    testEncodeIssuesRejectsMissingRequiredField();
    testEncodeLocalSnapshotRejectsMismatchedInterfaceName();
    return EXIT_SUCCESS;
}