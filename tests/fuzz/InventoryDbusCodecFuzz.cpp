#include "DbusFixtureJson.h"

#include <InventoryDbusCodec.h>

#include <jsoncpp/json/reader.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    const std::string input(reinterpret_cast<const char*>(data), size);
    if (!reader->parse(input.data(), input.data() + input.size(), &root, &errors)) {
        return 0;
    }

    try {
        if (root.isMember("snapshot")) {
            (void)RSCGroup::InventoryDbusCodec::decodeSnapshot(
                test_support::variantMapFromJsonObject(root["snapshot"]));
        } else if (root.isObject()) {
            (void)RSCGroup::InventoryDbusCodec::decodeSnapshot(
                test_support::variantMapFromJsonObject(root));
        }
    } catch (...) {
    }

    try {
        if (root.isMember("sourceStates")) {
            (void)RSCGroup::InventoryDbusCodec::decodeSourceStates(
                test_support::nestedVariantMapFromJsonObject(root["sourceStates"]));
        }
    } catch (...) {
    }

    try {
        if (root.isMember("issues")) {
            (void)RSCGroup::InventoryDbusCodec::decodeIssues(
                test_support::nestedVariantMapFromJsonObject(root["issues"]));
        }
    } catch (...) {
    }

    return 0;
}
