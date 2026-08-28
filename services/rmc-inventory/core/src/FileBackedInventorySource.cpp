#include "FileBackedInventorySource.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace RSCGroup {

FileBackedInventorySource::FileBackedInventorySource(std::string name,
                                                     bool required,
                                                     std::string filePath,
                                                     FieldNameList ownedFields)
    : name_(std::move(name))
    , required_(required)
    , filePath_(std::move(filePath))
    , ownedFields_(std::move(ownedFields))
{
    const auto path = std::filesystem::path(filePath_);
    if (filePath_.empty() || path.filename().empty()) {
        throw std::invalid_argument("FileBackedInventorySource '" + name_ + "': invalid file path");
    }
    state_.name = name_;
    state_.required = required_;
    state_.health = SourceHealth::FAILED;
    state_.origin = filePath_;
}

FileBackedInventorySource::~FileBackedInventorySource() = default;

InventoryFields FileBackedInventorySource::collect()
{
    std::ifstream in(filePath_);
    if (!in) {
        const std::string err = "cannot open '" + filePath_ + "'";
        noteFailure(err);
        throw std::runtime_error(err);
    }

    std::stringstream ss;
    ss << in.rdbuf();

    try {
        InventoryFields fields = fieldsFromContents(ss.str());
        noteSuccess();
        return fields;
    } catch (const std::exception& e) {
        noteFailure(e.what());
        throw;
    }
}

SourceState FileBackedInventorySource::getState() const
{
    std::scoped_lock lock(stateMutex_);
    return state_;
}

std::string FileBackedInventorySource::scalarFromContents(const std::string& contents, const std::string& what)
{
    static constexpr char kWs[] = " \t\r\n";
    const auto first = contents.find_first_not_of(kWs);
    if (first == std::string::npos) {
        throw std::runtime_error(what + ": empty content in file");
    }
    const auto last = contents.find_last_not_of(kWs);
    return contents.substr(first, last - first + 1);
}

void FileBackedInventorySource::noteSuccess() { std::scoped_lock lock(stateMutex_); state_.health = SourceHealth::OK; state_.lastError.reset(); }
void FileBackedInventorySource::noteFailure(const std::string& error) { std::scoped_lock lock(stateMutex_); state_.health = SourceHealth::FAILED; state_.lastError = error; }

} // namespace RSCGroup