#include "FileBackedInventorySource.h"

#include <filesystem>
#include <system_error>
#include <stdexcept>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

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
    try {
        InventoryFields fields = fieldsFromContents(readFileContents(filePath_));
        noteSuccess();
        return fields;
    } catch (const std::exception& e) {
        noteFailure(boundedErrorText(e.what()));
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

std::string FileBackedInventorySource::readFileContents(const std::string& filePath)
{
    const int fd = ::open(filePath.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        if (errno == ELOOP) {
            throw std::runtime_error("refusing to open symlink '" + filePath + "'");
        }
        throw std::system_error(errno, std::generic_category(),
                                "cannot open '" + filePath + "'");
    }

    struct FdCloser {
        int fd{-1};
        ~FdCloser() { if (fd >= 0) { ::close(fd); } }
    } closer{fd};

    struct stat st {};
    if (::fstat(fd, &st) != 0) {
        throw std::system_error(errno, std::generic_category(),
                                "cannot stat '" + filePath + "'");
    }
    if (!S_ISREG(st.st_mode)) {
        throw std::runtime_error("refusing non-regular file '" + filePath + "'");
    }
    if (st.st_size < 0 || static_cast<std::uintmax_t>(st.st_size) > kMaxFileBytes) {
        throw std::runtime_error("file exceeds size limit '" + filePath + "'");
    }

    std::string contents;
    contents.reserve(static_cast<std::size_t>(st.st_size));

    char buffer[4096];
    for (;;) {
        const ssize_t bytesRead = ::read(fd, buffer, sizeof(buffer));
        if (bytesRead == 0) {
            break;
        }
        if (bytesRead < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::system_error(errno, std::generic_category(),
                                    "cannot read '" + filePath + "'");
        }
        contents.append(buffer, static_cast<std::size_t>(bytesRead));
        if (contents.size() > kMaxFileBytes) {
            throw std::runtime_error("file exceeds size limit '" + filePath + "'");
        }
    }

    return contents;
}

std::string FileBackedInventorySource::boundedErrorText(std::string_view error)
{
    constexpr std::size_t kMaxErrorBytes = 512;
    if (error.size() <= kMaxErrorBytes) {
        return std::string(error);
    }
    std::string bounded(error.substr(0, kMaxErrorBytes));
    bounded += "...";
    return bounded;
}

void FileBackedInventorySource::noteSuccess() { std::scoped_lock lock(stateMutex_); state_.health = SourceHealth::OK; state_.lastError.reset(); }
void FileBackedInventorySource::noteFailure(const std::string& error) { std::scoped_lock lock(stateMutex_); state_.health = SourceHealth::FAILED; state_.lastError = error; }

} // namespace RSCGroup