#include "core/DeviceMetaFileSource.h"

#include <interop_contract/inventory.hpp>
#include <json/reader.h>

#include <memory>
#include <stdexcept>

namespace RSCGroup {
namespace {

namespace C = interop_contract::inventory;

void addStringField(InventoryFields& out,
                    const Json::Value& root,
                    const char* jsonKey,
                    std::string_view contractField)
{
    if (!root.isMember(jsonKey)) return;
    const auto& v = root[jsonKey];
    if (!v.isString() || v.asString().empty()) {
        throw std::runtime_error(std::string("device-meta-file: key '") + jsonKey +
                                 "' is not a non-empty string");
    }
    out.emplace(std::string(contractField), v.asString());
}

} // namespace

DeviceMetaFileSource::DeviceMetaFileSource(std::string filePath, bool required)
    : FileBackedInventorySource("device-meta-file", required, std::move(filePath),
          {std::string(C::FIELD_DEVICE_CLASS),
           std::string(C::FIELD_DEVICE_MODEL_ID),
           std::string(C::FIELD_DEVICE_PROJECT)})
{}

InventoryFields DeviceMetaFileSource::fieldsFromContents(const std::string& contents) const
{
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errs;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(contents.data(), contents.data() + contents.size(), &root, &errs)) {
        throw std::runtime_error("device-meta-file: JSON parse failed: " + errs);
    }
    if (!root.isObject()) {
        throw std::runtime_error("device-meta-file: root is not an object");
    }

    InventoryFields out;
    addStringField(out, root, "device_class",    C::FIELD_DEVICE_CLASS);
    addStringField(out, root, "device_model_id", C::FIELD_DEVICE_MODEL_ID);
    addStringField(out, root, "device_project",  C::FIELD_DEVICE_PROJECT);
    return out;
}

} // namespace RSCGroup