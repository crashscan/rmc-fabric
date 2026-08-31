#include "DbusFixtureJson.h"

#include <ClientResult.hpp>
#include <ContractVersion.hpp>
#include <DecodeError.hpp>
#include <IngressLimits.hpp>
#include <InventoryDbusCodec.h>
#include <NetworkObservationDbusCodec.h>
#include <inventory.hpp>
#include <network_observation/NetworkObservationContracts.hpp>
#include <network_observation/NetworkObservationEnumStrings.hpp>
#include <network_observation/NetworkObservationTypes.hpp>

#include <dbus-cxx/variant.h>

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {

using interop_contract::ClientErrorCode;
using interop_contract::DecodeError;
using interop_contract::DecodeErrorCode;
namespace inventory = interop_contract::inventory;
namespace observation = interop_contract::network_observation;
namespace inventory_codec = RSCGroup::InventoryDbusCodec;
namespace observation_codec = RSCGroup::NetworkObservationDbusCodec;

static_assert(static_cast<int>(ClientErrorCode::service_unavailable) == 0);
static_assert(static_cast<int>(ClientErrorCode::timeout) == 1);
static_assert(static_cast<int>(ClientErrorCode::transport_error) == 2);
static_assert(static_cast<int>(ClientErrorCode::decode_error) == 3);
static_assert(static_cast<int>(ClientErrorCode::invalid_response) == 4);

static_assert(static_cast<int>(DecodeErrorCode::missing_required_field) == 0);
static_assert(static_cast<int>(DecodeErrorCode::invalid_type) == 1);
static_assert(static_cast<int>(DecodeErrorCode::invalid_value) == 2);
static_assert(static_cast<int>(DecodeErrorCode::limit_exceeded) == 3);

static_assert(static_cast<int>(inventory::SourceHealth::OK) == 0);
static_assert(static_cast<int>(inventory::SourceHealth::DEGRADED) == 1);
static_assert(static_cast<int>(inventory::SourceHealth::FAILED) == 2);

static_assert(static_cast<int>(observation::CandidateClassification::Artifact) == 0);
static_assert(static_cast<int>(observation::CandidateClassification::LocalSelf) == 1);
static_assert(static_cast<int>(observation::CandidateClassification::WeakCandidate) == 2);
static_assert(static_cast<int>(observation::CandidateClassification::ProbableEndpoint) == 3);
static_assert(static_cast<int>(observation::CandidateClassification::RemoteEndpoint) == 4);
static_assert(static_cast<int>(observation::CandidateClassification::GatewayLike) == 5);
static_assert(static_cast<int>(observation::CandidateClassification::TopologyPeer) == 6);
static_assert(static_cast<int>(observation::CandidateClassification::Unknown) == 7);

static_assert(static_cast<int>(observation::CandidateStatus::Provisional) == 0);
static_assert(static_cast<int>(observation::CandidateStatus::Confirmed) == 1);
static_assert(static_cast<int>(observation::CandidateStatus::Aged) == 2);
static_assert(static_cast<int>(observation::CandidateStatus::Expired) == 3);
static_assert(static_cast<int>(observation::CandidateStatus::Removed) == 4);

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

std::string fixturePath(const std::string& name)
{
    return std::string(RMC_FABRIC_SOURCE_DIR) + "/tests/fixtures/" + name;
}

Json::Value loadFixture(const std::string& name)
{
    return test_support::loadJsonFile(fixturePath(name));
}

void expectJsonEquals(const Json::Value& actual,
                      const Json::Value& expected,
                      const std::string& label)
{
    if (actual != expected) {
        std::cerr << "JSON mismatch for " << label << '\n'
                  << "actual: " << actual.toStyledString() << '\n'
                  << "expected: " << expected.toStyledString() << '\n';
        std::exit(EXIT_FAILURE);
    }
}

template <typename Fn>
void expectDecodeError(Fn&& fn, DecodeErrorCode code, const std::string& label)
{
    try {
        fn();
    } catch (const DecodeError& error) {
        expect(error.code() == code, label + ": unexpected decode error code");
        return;
    }
    std::cerr << label << ": expected DecodeError\n";
    std::exit(EXIT_FAILURE);
}

void testApiContractSnapshot()
{
    const Json::Value root = loadFixture("api-contract-snapshot.json");
    expect(root["publicClientApiVersion"].asUInt() == interop_contract::PUBLIC_CLIENT_API_VERSION,
           "public client API version snapshot mismatch");
    expect(root["inventory"]["contractVersion"].asUInt() == inventory::CONTRACT_VERSION,
           "inventory contract version snapshot mismatch");
    expect(root["networkObservation"]["contractVersion"].asUInt() == observation::CONTRACT_VERSION,
           "network observation contract version snapshot mismatch");
    expect(root["inventory"]["issueCodes"]["loopStopped"].asString() ==
               std::string(inventory::ISSUE_CODE_LOOP_STOPPED),
           "inventory issue-code snapshot mismatch");
    expect(root["inventory"]["serviceName"].asString() == std::string(inventory::SERVICE_NAME),
           "inventory service name snapshot mismatch");
    expect(root["networkObservation"]["serviceName"].asString() == std::string(observation::SERVICE_NAME),
           "network observation service name snapshot mismatch");
    expect(root["clientErrorCode"]["invalid_response"].asInt() ==
               static_cast<int>(ClientErrorCode::invalid_response),
           "client error code snapshot mismatch");
}

void testInventoryFixtureRoundTrip(const std::string& name)
{
    const Json::Value root = loadFixture(name);
    expect(root["contractVersion"].asUInt() == inventory::CONTRACT_VERSION,
           "inventory fixture contract version mismatch");

    const auto snapshotMap = test_support::variantMapFromJsonObject(root["snapshot"]);
    const auto sourceStateMap = test_support::nestedVariantMapFromJsonObject(root["sourceStates"]);
    const auto issuesMap = test_support::nestedVariantMapFromJsonObject(root["issues"]);

    const auto decodedSnapshot = inventory_codec::decodeSnapshot(snapshotMap);
    const auto decodedStates = inventory_codec::decodeSourceStates(sourceStateMap);
    const auto decodedIssues = inventory_codec::decodeIssues(issuesMap);

    expectJsonEquals(test_support::jsonFromVariantMap(inventory_codec::encodeSnapshot(decodedSnapshot)),
                     root["snapshot"],
                     name + ":snapshot");
    expectJsonEquals(test_support::jsonFromNestedVariantMap(inventory_codec::encodeSourceStates(decodedStates)),
                     root["sourceStates"],
                     name + ":sourceStates");
    expectJsonEquals(test_support::jsonFromNestedVariantMap(inventory_codec::encodeIssues(decodedIssues)),
                     root["issues"],
                     name + ":issues");
}

void testNetworkFixtureRoundTrip(const std::string& name)
{
    const Json::Value root = loadFixture(name);
    expect(root["contractVersion"].asUInt() == observation::CONTRACT_VERSION,
           "network fixture contract version mismatch");

    const auto localSnapshotMap = test_support::variantMapFromJsonObject(root["localSnapshot"]);
    const auto candidateMap = test_support::variantMapFromJsonObject(root["candidate"]);

    const auto decodedSnapshot = observation_codec::fromVariantMapLocalSnapshot(localSnapshotMap);
    const auto decodedCandidate = observation_codec::fromVariantMapCandidate(candidateMap);

    test_support::VariantMap reencodedSnapshot;
    for (const auto& [ifname, iface] : decodedSnapshot.interfaces) {
        reencodedSnapshot.emplace(ifname, DBus::Variant(observation_codec::toVariantMap(iface)));
    }

    expectJsonEquals(test_support::jsonFromVariantMap(reencodedSnapshot),
                     root["localSnapshot"],
                     name + ":localSnapshot");
    expectJsonEquals(test_support::jsonFromVariantMap(observation_codec::toVariantMap(decodedCandidate)),
                     root["candidate"],
                     name + ":candidate");
}

void testInventoryEncodingIsCanonical()
{
    inventory::InventoryIssues lhs;
    lhs.emplace("uuid-file", inventory::InventoryFields{
        {std::string(inventory::ISSUE_MESSAGE), std::string("missing")},
        {std::string(inventory::ISSUE_SEVERITY), std::string(inventory::SEVERITY_ERROR)},
    });
    lhs.emplace("firmware-file", inventory::InventoryFields{
        {std::string(inventory::ISSUE_SEVERITY), std::string(inventory::SEVERITY_WARNING)},
        {std::string(inventory::ISSUE_MESSAGE), std::string("stale")},
    });

    inventory::InventoryIssues rhs;
    rhs.emplace("firmware-file", inventory::InventoryFields{
        {std::string(inventory::ISSUE_MESSAGE), std::string("stale")},
        {std::string(inventory::ISSUE_SEVERITY), std::string(inventory::SEVERITY_WARNING)},
    });
    rhs.emplace("uuid-file", inventory::InventoryFields{
        {std::string(inventory::ISSUE_SEVERITY), std::string(inventory::SEVERITY_ERROR)},
        {std::string(inventory::ISSUE_MESSAGE), std::string("missing")},
    });

    expectJsonEquals(test_support::jsonFromNestedVariantMap(inventory_codec::encodeIssues(lhs)),
                     test_support::jsonFromNestedVariantMap(inventory_codec::encodeIssues(rhs)),
                     "inventory canonical issue encoding");
}

void testNetworkEncodingIsCanonical()
{
    observation::RemoteCandidate lhs;
    lhs.mac = "00:11:22:33:44:55";
    lhs.classification = observation::CandidateClassification::RemoteEndpoint;
    lhs.status = observation::CandidateStatus::Confirmed;
    lhs.neighborIfaces.insert("eth1");
    lhs.neighborIfaces.insert("eth0");
    lhs.ipv4.insert("10.0.0.2/24");
    lhs.ipv4.insert("10.0.0.3/24");

    observation::RemoteCandidate rhs = lhs;
    rhs.neighborIfaces.clear();
    rhs.ipv4.clear();
    rhs.neighborIfaces.insert("eth0");
    rhs.neighborIfaces.insert("eth1");
    rhs.ipv4.insert("10.0.0.3/24");
    rhs.ipv4.insert("10.0.0.2/24");

    expectJsonEquals(test_support::jsonFromVariantMap(observation_codec::toVariantMap(lhs)),
                     test_support::jsonFromVariantMap(observation_codec::toVariantMap(rhs)),
                     "network canonical candidate encoding");
}

void testInventoryIngressLimits()
{
    inventory_codec::VariantMap fields;
    for (std::size_t index = 0; index < interop_contract::ingress::inventory::kMaxFields + 1; ++index) {
        fields.emplace("key-" + std::to_string(index), DBus::Variant(std::string("value")));
    }
    expectDecodeError([&] { (void)inventory_codec::decodeFields(fields); },
                      DecodeErrorCode::limit_exceeded,
                      "inventory oversized field map");

    inventory_codec::VariantMap snapshot;
    snapshot.emplace(std::string(inventory::FIELD_VERSION), DBus::Variant(std::uint64_t{7}));
    snapshot.emplace(std::string(inventory::FIELD_TIMESTAMP), DBus::Variant(std::numeric_limits<std::uint64_t>::max()));
    snapshot.emplace(std::string(inventory::FIELD_READY), DBus::Variant(true));
    snapshot.emplace(std::string(inventory::FIELD_PHASE), DBus::Variant(std::string("live")));
    expectDecodeError([&] { (void)inventory_codec::decodeSnapshot(snapshot); },
                      DecodeErrorCode::invalid_value,
                      "inventory timestamp overflow");

    snapshot[std::string(inventory::FIELD_TIMESTAMP)] = DBus::Variant(std::int64_t{1});
    snapshot["oversized"] = DBus::Variant(std::string(interop_contract::ingress::kMaxStringLength + 1, 'x'));
    expectDecodeError([&] { (void)inventory_codec::decodeSnapshot(snapshot); },
                      DecodeErrorCode::limit_exceeded,
                      "inventory oversized string");
}

void testNetworkIngressLimits()
{
    std::map<std::string, DBus::Variant> iface;
    iface.emplace(std::string(observation::K_IFINDEX), DBus::Variant(std::int32_t{7}));
    iface.emplace(std::string(observation::K_IFNAME), DBus::Variant(std::string("eth0")));
    iface.emplace(std::string(observation::K_MAC), DBus::Variant(std::string("aa:bb:cc:dd:ee:ff")));
    iface.emplace(std::string(observation::K_ADMINUP), DBus::Variant(true));
    iface.emplace(std::string(observation::K_RUNNING), DBus::Variant(true));
    iface.emplace(std::string(observation::K_OPERSTATE), DBus::Variant(std::string("UP")));
    std::vector<DBus::Variant> ip4;
    for (std::size_t index = 0; index < interop_contract::ingress::network_observation::kMaxStringSetEntries + 1; ++index) {
        ip4.emplace_back(std::string("10.0.0.") + std::to_string(index) + "/24");
    }
    iface.emplace(std::string(observation::K_IPV4), DBus::Variant(ip4));
    iface.emplace(std::string(observation::K_IPV6), DBus::Variant(std::vector<DBus::Variant>{}));
    expectDecodeError([&] { (void)observation_codec::fromVariantMapIface(iface); },
                      DecodeErrorCode::limit_exceeded,
                      "network oversized string set");

    std::map<std::string, DBus::Variant> candidate;
    candidate.emplace(std::string(observation::K_MAC), DBus::Variant(std::string("00:11:22:33:44:55")));
    candidate.emplace(std::string(observation::K_CLASSIFICATION), DBus::Variant(std::string("Broken")));
    candidate.emplace(std::string(observation::K_STATUS), DBus::Variant(std::string("Confirmed")));
    candidate.emplace(std::string(observation::K_SEEN_IN_FDB), DBus::Variant(true));
    candidate.emplace(std::string(observation::K_SEEN_IN_NEIGH), DBus::Variant(true));
    candidate.emplace(std::string(observation::K_SEEN_IN_LLDP), DBus::Variant(false));
    candidate.emplace(std::string(observation::K_NEIGHBOR_IFACES), DBus::Variant(std::vector<DBus::Variant>{}));
    candidate.emplace(std::string(observation::K_IPV4), DBus::Variant(std::vector<DBus::Variant>{}));
    candidate.emplace(std::string(observation::K_IPV6), DBus::Variant(std::vector<DBus::Variant>{}));
    expectDecodeError([&] { (void)observation_codec::fromVariantMapCandidate(candidate); },
                      DecodeErrorCode::invalid_value,
                      "network unknown enum");
}

void testDecodeErrorsDoNotReturnPartialObjects()
{
    inventory_codec::VariantMap snapshot;
    snapshot.emplace(std::string(inventory::FIELD_VERSION), DBus::Variant(std::uint64_t{1}));
    snapshot.emplace(std::string(inventory::FIELD_TIMESTAMP), DBus::Variant(std::int64_t{1}));
    snapshot.emplace(std::string(inventory::FIELD_READY), DBus::Variant(true));
    snapshot.emplace(std::string(inventory::FIELD_PHASE), DBus::Variant(std::string("live")));
    snapshot.emplace("nodeName", DBus::Variant(std::vector<DBus::Variant>{}));

    bool returned = false;
    try {
        (void)inventory_codec::decodeSnapshot(snapshot);
        returned = true;
    } catch (const DecodeError&) {
    }
    expect(!returned, "inventory decode should not return partial snapshot");

    std::map<std::string, DBus::Variant> iface;
    iface.emplace(std::string(observation::K_IFINDEX), DBus::Variant(std::string("not-an-int")));
    iface.emplace(std::string(observation::K_IFNAME), DBus::Variant(std::string("eth0")));
    iface.emplace(std::string(observation::K_MAC), DBus::Variant(std::string("aa:bb:cc:dd:ee:ff")));
    iface.emplace(std::string(observation::K_ADMINUP), DBus::Variant(true));
    iface.emplace(std::string(observation::K_RUNNING), DBus::Variant(true));
    iface.emplace(std::string(observation::K_OPERSTATE), DBus::Variant(std::string("UP")));
    iface.emplace(std::string(observation::K_IPV4), DBus::Variant(std::vector<DBus::Variant>{}));
    iface.emplace(std::string(observation::K_IPV6), DBus::Variant(std::vector<DBus::Variant>{}));

    returned = false;
    try {
        (void)observation_codec::fromVariantMapIface(iface);
        returned = true;
    } catch (const DecodeError&) {
    }
    expect(!returned, "network decode should not return partial interface");
}

} // namespace

int main()
{
    testApiContractSnapshot();
    testInventoryFixtureRoundTrip("inventory-v1-current.json");
    testInventoryFixtureRoundTrip("inventory-v1-historical.json");
    testNetworkFixtureRoundTrip("network-observation-v1-current.json");
    testNetworkFixtureRoundTrip("network-observation-v1-historical.json");
    testInventoryEncodingIsCanonical();
    testNetworkEncodingIsCanonical();
    testInventoryIngressLimits();
    testNetworkIngressLimits();
    testDecodeErrorsDoNotReturnPartialObjects();
    return EXIT_SUCCESS;
}
