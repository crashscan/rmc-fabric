#include "DbusFixtureJson.h"

#include <NetworkObservationDbusCodec.h>

#include <json/reader.h>

#include <cstddef>
#include <cstdint>
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
        if (root.isMember("localSnapshot")) {
            (void)RSCGroup::NetworkObservationDbusCodec::fromVariantMapLocalSnapshot(
                test_support::variantMapFromJsonObject(root["localSnapshot"]));
        } else if (root.isObject()) {
            (void)RSCGroup::NetworkObservationDbusCodec::fromVariantMapLocalSnapshot(
                test_support::variantMapFromJsonObject(root));
        }
    } catch (...) {
    }

    try {
        if (root.isMember("candidate")) {
            (void)RSCGroup::NetworkObservationDbusCodec::fromVariantMapCandidate(
                test_support::variantMapFromJsonObject(root["candidate"]));
        }
    } catch (...) {
    }

    return 0;
}
