#include "inventory.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using interop_contract::inventory::FieldValue;
using interop_contract::inventory::InventoryFields;
using interop_contract::inventory::InventoryIssueFields;
using interop_contract::inventory::InventoryIssues;
using interop_contract::inventory::InventorySnapshot;
using interop_contract::inventory::SourceHealth;
using interop_contract::inventory::SourceState;
using interop_contract::inventory::contains_reserved_metadata_fields;
using interop_contract::inventory::first_reserved_metadata_field;
using interop_contract::inventory::get_field_value;
using interop_contract::inventory::is_metadata_field;
using interop_contract::inventory::make_single_field_map;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(EXIT_FAILURE);
    }
}

InventorySnapshot makeSampleSnapshot()
{
    InventorySnapshot snapshot;
    snapshot.version = 11;
    snapshot.timestamp = 123456789;
    snapshot.ready = true;
    snapshot.phase = "live";
    snapshot.fields.emplace("uuid", FieldValue{std::string("1234-5678")});
    snapshot.fields.emplace("nodeName", FieldValue{std::string("rack12-node7")});
    return snapshot;
}

void testInventoryTypesSupportMixedValues()
{
    InventoryFields fields;
    fields.emplace("boolField", FieldValue{true});
    fields.emplace("signedField", FieldValue{int64_t{-12}});
    fields.emplace("unsignedField", FieldValue{uint64_t{44}});
    fields.emplace("stringField", FieldValue{std::string("value")});

    expect(std::get<bool>(fields.at("boolField")) == true, "unexpected boolField");
    expect(std::get<int64_t>(fields.at("signedField")) == -12, "unexpected signedField");
    expect(std::get<uint64_t>(fields.at("unsignedField")) == 44, "unexpected unsignedField");
    expect(std::get<std::string>(fields.at("stringField")) == "value", "unexpected stringField");
}

void testInventoryIssueFieldsAliasCarriesFieldValues()
{
    InventoryIssueFields fields;
    fields.emplace(std::string(interop_contract::inventory::ISSUE_SEVERITY),
                   std::string(interop_contract::inventory::SEVERITY_ERROR));
    fields.emplace(std::string(interop_contract::inventory::ISSUE_MESSAGE),
                   std::string("cannot open '/etc/rmc/uuid'"));
    fields.emplace(std::string(interop_contract::inventory::ISSUE_ORIGIN),
                   std::string("/etc/rmc/uuid"));

    expect(fields.size() == 3, "expected 3 issue fields");
    expect(std::get<std::string>(fields.at(std::string(interop_contract::inventory::ISSUE_SEVERITY))) ==
               std::string(interop_contract::inventory::SEVERITY_ERROR),
           "unexpected severity");
    expect(std::get<std::string>(fields.at(std::string(interop_contract::inventory::ISSUE_MESSAGE))) ==
               "cannot open '/etc/rmc/uuid'",
           "unexpected message");
    expect(std::get<std::string>(fields.at(std::string(interop_contract::inventory::ISSUE_ORIGIN))) ==
               "/etc/rmc/uuid",
           "unexpected origin");
}

void testInventoryIssuesMapUsesDeterministicSourceKeyOrdering()
{
    InventoryIssues issues;
    issues.emplace("uuid-file", InventoryIssueFields{
        {std::string(interop_contract::inventory::ISSUE_SEVERITY),
         std::string(interop_contract::inventory::SEVERITY_ERROR)}
    });
    issues.emplace("firmware-file", InventoryIssueFields{
        {std::string(interop_contract::inventory::ISSUE_SEVERITY),
         std::string(interop_contract::inventory::SEVERITY_WARNING)}
    });

    expect(issues.size() == 2, "expected 2 issues");

    auto it = issues.begin();
    expect(it != issues.end(), "expected first issue");
    expect(it->first == "firmware-file", "expected lexicographically first issue key");
    ++it;
    expect(it != issues.end(), "expected second issue");
    expect(it->first == "uuid-file", "expected lexicographically second issue key");
}

void testMetadataFieldRecognition()
{
    expect(is_metadata_field(std::string_view("version")),
           "expected version to be recognized as metadata");
    expect(is_metadata_field(std::string_view("timestamp")),
           "expected timestamp to be recognized as metadata");
    expect(is_metadata_field(std::string_view("ready")),
           "expected ready to be recognized as metadata");
    expect(is_metadata_field(std::string_view("phase")),
           "expected phase to be recognized as metadata");

    expect(!is_metadata_field(std::string_view("uuid")),
           "did not expect uuid to be metadata");
    expect(!is_metadata_field(std::string_view("nodeName")),
           "did not expect nodeName to be metadata");
}

void testReservedMetadataFieldValidation()
{
    InventoryFields sourceFields{
        {"uuid", FieldValue{std::string("1234-5678")}},
        {"nodeName", FieldValue{std::string("rack12-node7")}}
    };

    expect(!contains_reserved_metadata_fields(sourceFields),
           "expected no reserved metadata fields");
    expect(first_reserved_metadata_field(sourceFields).empty(),
           "expected no first reserved metadata field");

    InventoryFields withMetadata{
        {"uuid", FieldValue{std::string("1234-5678")}},
        {"version", FieldValue{uint64_t{7}}},
        {"nodeName", FieldValue{std::string("rack12-node7")}}
    };

    expect(contains_reserved_metadata_fields(withMetadata),
           "expected reserved metadata field detection");
    expect(first_reserved_metadata_field(withMetadata) == "version",
           "expected version to be first reserved metadata field");
}

void testSnapshotDefaultsAndStorage()
{
    const InventorySnapshot empty;
    expect(empty.version == 0, "expected default version to be 0");
    expect(empty.timestamp == 0, "expected default timestamp to be 0");
    expect(empty.ready == false, "expected default ready to be false");
    expect(empty.phase.empty(), "expected default phase to be empty");
    expect(empty.fields.empty(), "expected default fields to be empty");

    InventorySnapshot snapshot;
    snapshot.version = 7;
    snapshot.timestamp = 123456789;
    snapshot.ready = true;
    snapshot.phase = "live";
    snapshot.fields.emplace("uuid", FieldValue{std::string("1234-5678")});

    expect(snapshot.version == 7, "unexpected version");
    expect(snapshot.timestamp == 123456789, "unexpected timestamp");
    expect(snapshot.ready == true, "unexpected ready");
    expect(snapshot.phase == "live", "unexpected phase");
    expect(std::get<std::string>(snapshot.fields.at("uuid")) == "1234-5678",
           "unexpected uuid field");
}

void testFieldAccessHelpers()
{
    const auto snapshot = makeSampleSnapshot();

    const auto uuid = get_field_value(snapshot, "uuid");
    expect(uuid.has_value(), "expected uuid field to be present");
    expect(std::get<std::string>(*uuid) == "1234-5678", "unexpected uuid value");

    const auto version = get_field_value(snapshot, "version");
    expect(version.has_value(), "expected version to be present");
    expect(std::get<uint64_t>(*version) == 11, "unexpected version value");

    const auto unknown = get_field_value(snapshot, "unknownField");
    expect(!unknown.has_value(), "did not expect unknown field to be present");

    const InventoryFields uuidMap = make_single_field_map(snapshot, "uuid");
    expect(uuidMap.size() == 1, "expected one field in singleton map");
    expect(std::get<std::string>(uuidMap.at("uuid")) == "1234-5678",
           "unexpected singleton uuid value");
}

void testInventoryUmbrellaHeaderSurface()
{
    InventorySnapshot snapshot;
    snapshot.version = 3;
    snapshot.timestamp = 444;
    snapshot.ready = true;
    snapshot.phase = std::string(interop_contract::inventory::PHASE_LIVE);
    snapshot.fields.emplace("uuid", FieldValue{std::string("abc-123")});

    expect(is_metadata_field(std::string_view("version")),
           "expected umbrella header to expose metadata helper");
    expect(!contains_reserved_metadata_fields(snapshot.fields),
           "did not expect source-owned fields to contain reserved metadata");

    const auto ready = get_field_value(snapshot, "ready");
    expect(ready.has_value(), "expected ready via umbrella header");
    expect(std::get<bool>(*ready) == true,
           "unexpected ready value via umbrella header");

    InventoryIssueFields issueFields;
    issueFields.emplace(std::string(interop_contract::inventory::ISSUE_SEVERITY),
                        std::string(interop_contract::inventory::SEVERITY_WARNING));
    InventoryIssues issues;
    issues.emplace("software-file", issueFields);

    expect(issues.size() == 1, "expected one issue via umbrella header");
}

void testSourceStateContractTypes()
{
    SourceState state;
    state.name = "firmware-file";
    state.required = true;
    state.stale = false;
    state.health = SourceHealth::OK;
    state.lastAttemptTs = 100;
    state.lastSuccessTs = 99;
    state.lastError.reset();
    state.origin = "/etc/rmc/firmware";

    expect(state.name == "firmware-file", "unexpected source state name");
    expect(state.required == true, "unexpected source state required flag");
    expect(state.stale == false, "unexpected source state stale flag");
    expect(state.health == SourceHealth::OK, "unexpected source health");
    expect(state.lastAttemptTs == 100, "unexpected source attempt timestamp");
    expect(state.lastSuccessTs == 99, "unexpected source success timestamp");
    expect(state.origin.has_value() && *state.origin == "/etc/rmc/firmware", "unexpected source origin");
}

} // namespace

int main()
{
    testInventoryTypesSupportMixedValues();
    testInventoryIssueFieldsAliasCarriesFieldValues();
    testInventoryIssuesMapUsesDeterministicSourceKeyOrdering();
    testMetadataFieldRecognition();
    testReservedMetadataFieldValidation();
    testSnapshotDefaultsAndStorage();
    testFieldAccessHelpers();
    testInventoryUmbrellaHeaderSurface();
    testSourceStateContractTypes();
    return EXIT_SUCCESS;
}