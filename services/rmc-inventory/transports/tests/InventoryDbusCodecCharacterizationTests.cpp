#include "InventoryDbusCodec.h"
#include <inventory.hpp>
#include <DecodeError.hpp>

#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <dbus-cxx/enums.h>
#include <dbus-cxx/variant.h>

namespace {

using RSCGroup::InventoryDbusCodec::decodeIssues;
using RSCGroup::InventoryDbusCodec::encodeIssues;

using InventoryFields = interop_contract::inventory::InventoryFields;
using Issues = interop_contract::inventory::InventoryIssues;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(EXIT_FAILURE);
    }
}

std::string typeName(const DBus::Variant& value)
{
    switch (value.type()) {
        case DBus::DataType::BOOLEAN:
            return "bool";
        case DBus::DataType::INT32:
            return "int32";
        case DBus::DataType::INT64:
            return "int64";
        case DBus::DataType::UINT32:
            return "uint32";
        case DBus::DataType::UINT64:
            return "uint64";
        case DBus::DataType::STRING:
            return "string";
        default:
            return "other";
    }
}

void expectStringField(const std::map<std::string, DBus::Variant>& issue,
                       const std::string& key,
                       const std::string& expectedValue)
{
    const auto it = issue.find(key);
    expect(it != issue.end(), "missing expected key: " + key);
    expect(it->second.type() == DBus::DataType::STRING,
           "expected key '" + key + "' to encode as string but got " + typeName(it->second));
    expect(it->second.to_string() == expectedValue,
           "unexpected string value for key '" + key + "'");
}

void testEncodeIssuesPreservesDeterministicStructure()
{
    Issues issues;

    issues.emplace("firmware-file", InventoryFields{
        {std::string(interop_contract::inventory::ISSUE_MESSAGE), std::string("cannot open '/etc/rmc/firmware'")},
        {std::string(interop_contract::inventory::ISSUE_ORIGIN), std::string("/etc/rmc/firmware")},
        {std::string(interop_contract::inventory::ISSUE_SEVERITY), std::string(interop_contract::inventory::SEVERITY_ERROR)}
    });

    issues.emplace("software-file", InventoryFields{
        {std::string(interop_contract::inventory::ISSUE_MESSAGE), std::string("source data is stale")},
        {std::string(interop_contract::inventory::ISSUE_ORIGIN), std::string("/etc/rmc/software")},
        {std::string(interop_contract::inventory::ISSUE_SEVERITY), std::string(interop_contract::inventory::SEVERITY_WARNING)}
    });

    const auto encoded = encodeIssues(issues);

    expect(encoded.size() == 2, "expected exactly 2 encoded issues");

    auto outerIt = encoded.begin();
    expect(outerIt->first == "firmware-file", "expected first issue key to be firmware-file");
    expect((++outerIt)->first == "software-file", "expected second issue key to be software-file");

    const auto firmwareIt = encoded.find("firmware-file");
    expect(firmwareIt != encoded.end(), "missing firmware-file issue");
    expect(firmwareIt->second.size() == 3, "expected firmware-file issue to have 3 fields");
    expectStringField(firmwareIt->second,
                      std::string(interop_contract::inventory::ISSUE_SEVERITY),
                      std::string(interop_contract::inventory::SEVERITY_ERROR));
    expectStringField(firmwareIt->second,
                      std::string(interop_contract::inventory::ISSUE_MESSAGE),
                      "cannot open '/etc/rmc/firmware'");
    expectStringField(firmwareIt->second,
                      std::string(interop_contract::inventory::ISSUE_ORIGIN),
                      "/etc/rmc/firmware");

    const auto softwareIt = encoded.find("software-file");
    expect(softwareIt != encoded.end(), "missing software-file issue");
    expect(softwareIt->second.size() == 3, "expected software-file issue to have 3 fields");
    expectStringField(softwareIt->second,
                      std::string(interop_contract::inventory::ISSUE_SEVERITY),
                      std::string(interop_contract::inventory::SEVERITY_WARNING));
    expectStringField(softwareIt->second,
                      std::string(interop_contract::inventory::ISSUE_MESSAGE),
                      "source data is stale");
    expectStringField(softwareIt->second,
                      std::string(interop_contract::inventory::ISSUE_ORIGIN),
                      "/etc/rmc/software");
}

void testEncodeDecodeIssuesRoundTripsContractShape()
{
    Issues issues;
    issues.emplace("uuid-file", InventoryFields{
        {std::string(interop_contract::inventory::ISSUE_SEVERITY), std::string(interop_contract::inventory::SEVERITY_ERROR)},
        {std::string(interop_contract::inventory::ISSUE_MESSAGE), std::string("cannot open '/etc/rmc/uuid'")},
        {std::string(interop_contract::inventory::ISSUE_ORIGIN), std::string("/etc/rmc/uuid")}
    });

    const auto encoded = encodeIssues(issues);
    const auto decoded = decodeIssues(encoded);

    expect(decoded.size() == 1, "expected one decoded issue");
    const auto it = decoded.find("uuid-file");
    expect(it != decoded.end(), "missing decoded uuid-file issue");
    expect(it->second.size() == 3, "expected decoded uuid-file issue to have 3 fields");

    expect(std::get<std::string>(it->second.at(std::string(interop_contract::inventory::ISSUE_SEVERITY))) ==
               std::string(interop_contract::inventory::SEVERITY_ERROR),
           "unexpected decoded severity");
    expect(std::get<std::string>(it->second.at(std::string(interop_contract::inventory::ISSUE_MESSAGE))) ==
               "cannot open '/etc/rmc/uuid'",
           "unexpected decoded message");
    expect(std::get<std::string>(it->second.at(std::string(interop_contract::inventory::ISSUE_ORIGIN))) ==
               "/etc/rmc/uuid",
           "unexpected decoded origin");
}

void testDecodeSnapshotRejectsMissingMetadata()
{
    std::map<std::string, DBus::Variant> raw;
    raw[std::string(interop_contract::inventory::FIELD_TIMESTAMP)] = DBus::Variant(int64_t{9});
    raw[std::string(interop_contract::inventory::FIELD_READY)] = DBus::Variant(true);
    raw[std::string(interop_contract::inventory::FIELD_PHASE)] = DBus::Variant(std::string("live"));

    bool threw = false;
    try {
        (void)RSCGroup::InventoryDbusCodec::decodeSnapshot(raw);
    } catch (const interop_contract::DecodeError&) {
        threw = true;
    }
    expect(threw, "decodeSnapshot should reject missing version metadata");
}

void testDecodeSourceStateRejectsUnknownHealth()
{
    std::map<std::string, DBus::Variant> raw;
    raw[std::string(interop_contract::inventory::SOURCE_STATE_HEALTH)] = DBus::Variant(std::string("mystery"));
    raw[std::string(interop_contract::inventory::SOURCE_STATE_REQUIRED)] = DBus::Variant(true);
    raw[std::string(interop_contract::inventory::SOURCE_STATE_STALE)] = DBus::Variant(false);
    raw[std::string(interop_contract::inventory::SOURCE_STATE_LAST_ATTEMPT_TS)] = DBus::Variant(int64_t{1});
    raw[std::string(interop_contract::inventory::SOURCE_STATE_LAST_SUCCESS_TS)] = DBus::Variant(int64_t{1});

    bool threw = false;
    try {
        (void)RSCGroup::InventoryDbusCodec::decodeSourceState(raw, "firmware");
    } catch (const interop_contract::DecodeError&) {
        threw = true;
    }
    expect(threw, "decodeSourceState should reject unknown health values");
}

} // namespace

int main()
{
    testEncodeIssuesPreservesDeterministicStructure();
    testEncodeDecodeIssuesRoundTripsContractShape();
    testDecodeSnapshotRejectsMissingMetadata();
    testDecodeSourceStateRejectsUnknownHealth();
    return EXIT_SUCCESS;
}
