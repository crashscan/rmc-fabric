#pragma once

#include "core/IInventorySource.h"
#include "core/IWatchableInventorySource.h"

#include <mutex>
#include <string>

namespace RSCGroup {

class FileBackedInventorySource : public IInventorySource, public IWatchableInventorySource {
public:
    FileBackedInventorySource(std::string name,
                              bool required,
                              std::string filePath,
                              FieldNameList ownedFields);
    ~FileBackedInventorySource() override;

    FileBackedInventorySource(const FileBackedInventorySource&) = delete;
    FileBackedInventorySource& operator=(const FileBackedInventorySource&) = delete;

    [[nodiscard]] std::string getName() const override { return name_; }
    [[nodiscard]] bool isRequired() const override { return required_; }
    [[nodiscard]] FieldNameList getOwnedFields() const override { return ownedFields_; }
    [[nodiscard]] InventoryFields collect() override;
    [[nodiscard]] SourceState getState() const override;

    [[nodiscard]] std::string getWatchPath() const override { return filePath_; }
    [[nodiscard]] const std::string& getFilePath() const { return filePath_; }

protected:
    [[nodiscard]] virtual InventoryFields fieldsFromContents(const std::string& contents) const = 0;
    [[nodiscard]] static std::string scalarFromContents(const std::string& contents,
                                                        const std::string& what);

private:
    void noteSuccess();
    void noteFailure(const std::string& error);

    const std::string name_;
    const bool required_;
    const std::string filePath_;
    const FieldNameList ownedFields_;

    mutable std::mutex stateMutex_;
    SourceState state_;
};

} // namespace RSCGroup