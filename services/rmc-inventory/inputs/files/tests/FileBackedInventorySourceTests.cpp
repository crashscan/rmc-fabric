#include "ScalarFileSource.h"

#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using RSCGroup::ScalarFileSource;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

class TempDir {
public:
    TempDir()
    {
        std::string pattern = "/tmp/rmc-fabric-file-source-XXXXXX";
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        const char* created = ::mkdtemp(buffer.data());
        if (!created) {
            throw std::runtime_error("mkdtemp failed");
        }
        path_ = created;
    }

    ~TempDir()
    {
        if (!path_.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(path_, ec);
        }
    }

    [[nodiscard]] const std::string& path() const { return path_; }

private:
    std::string path_;
};

void writeFile(const std::string& path, const std::string& contents)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    expect(static_cast<bool>(out), "failed to open test file: " + path);
    out << contents;
    expect(static_cast<bool>(out), "failed to write test file: " + path);
}

void testCollectReadsRegularFile()
{
    TempDir dir;
    const std::string path = dir.path() + "/node-name";
    writeFile(path, " rack12-node7 \n");

    ScalarFileSource source("node-name-file", false, path, "nodeName");
    const auto fields = source.collect();

    expect(fields.size() == 1, "expected exactly one collected field");
    expect(std::get<std::string>(fields.at("nodeName")) == "rack12-node7",
           "expected trimmed scalar value");
}

void testCollectRejectsSymlink()
{
    TempDir dir;
    const std::string target = dir.path() + "/target";
    const std::string link = dir.path() + "/uuid";
    writeFile(target, "1234-5678");
    expect(::symlink(target.c_str(), link.c_str()) == 0, "failed to create symlink");

    ScalarFileSource source("uuid-file", true, link, "uuid");
    bool threw = false;
    try {
        (void)source.collect();
    } catch (const std::exception& e) {
        threw = std::string(e.what()).find("symlink") != std::string::npos;
    }
    expect(threw, "expected symlink-backed source to be rejected");
}

void testCollectRejectsDirectory()
{
    TempDir dir;
    const std::string path = dir.path() + "/firmware";
    expect(::mkdir(path.c_str(), 0700) == 0, "failed to create directory fixture");

    ScalarFileSource source("firmware-file", true, path, "firmwareVersion");
    bool threw = false;
    try {
        (void)source.collect();
    } catch (const std::exception& e) {
        threw = std::string(e.what()).find("non-regular") != std::string::npos;
    }
    expect(threw, "expected directory-backed source to be rejected");
}

void testCollectRejectsFifo()
{
    TempDir dir;
    const std::string path = dir.path() + "/software";
    expect(::mkfifo(path.c_str(), 0600) == 0, "failed to create fifo fixture");

    ScalarFileSource source("software-file", false, path, "softwareVersion");
    bool threw = false;
    try {
        (void)source.collect();
    } catch (const std::exception& e) {
        threw = std::string(e.what()).find("non-regular") != std::string::npos;
    }
    expect(threw, "expected fifo-backed source to be rejected");
}

void testCollectRejectsOversizedFile()
{
    TempDir dir;
    const std::string path = dir.path() + "/device-meta.json";
    writeFile(path, std::string(70 * 1024, 'x'));

    ScalarFileSource source("node-name-file", false, path, "nodeName");
    bool threw = false;
    try {
        (void)source.collect();
    } catch (const std::exception& e) {
        threw = std::string(e.what()).find("size limit") != std::string::npos;
    }
    expect(threw, "expected oversized file to be rejected");
}

void testCollectHandlesRenameReplacement()
{
    TempDir dir;
    const std::string path = dir.path() + "/node-name";
    const std::string replacement = dir.path() + "/node-name.tmp";
    writeFile(path, "node-a");

    ScalarFileSource source("node-name-file", false, path, "nodeName");
    expect(std::get<std::string>(source.collect().at("nodeName")) == "node-a",
           "expected original value before replacement");

    writeFile(replacement, "node-b");
    expect(::rename(replacement.c_str(), path.c_str()) == 0, "failed to replace watched file");

    expect(std::get<std::string>(source.collect().at("nodeName")) == "node-b",
           "expected replaced file contents after atomic rename");
}

} // namespace

int main()
{
    testCollectReadsRegularFile();
    testCollectRejectsSymlink();
    testCollectRejectsDirectory();
    testCollectRejectsFifo();
    testCollectRejectsOversizedFile();
    testCollectHandlesRenameReplacement();
    return EXIT_SUCCESS;
}
