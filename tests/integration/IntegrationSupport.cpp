#include "IntegrationSupport.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace integration_support {

TempDir::TempDir()
{
    const char* tmpdir = std::getenv("TMPDIR");
    std::string pattern = std::string((tmpdir && *tmpdir) ? tmpdir : "/tmp") +
        "/rmc-fabric-integration-XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const char* created = ::mkdtemp(buffer.data());
    if (!created) {
        throw std::runtime_error("mkdtemp failed");
    }
    path_ = created;
}

TempDir::~TempDir()
{
    if (!path_.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
}

PrivateBus::PrivateBus(const std::string& socketPath)
{
    const std::string command =
        "dbus-daemon --session --fork --print-address=1 --print-pid=1 --address=" +
        shellQuote("unix:path=" + socketPath);
    FILE* pipe = ::popen(command.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("failed to start private dbus-daemon");
    }

    char firstBuffer[1024];
    char secondBuffer[1024];
    const bool haveFirst = std::fgets(firstBuffer, sizeof(firstBuffer), pipe) != nullptr;
    const bool haveSecond = std::fgets(secondBuffer, sizeof(secondBuffer), pipe) != nullptr;
    const int rc = ::pclose(pipe);
    if (!haveFirst || !haveSecond || rc != 0) {
        throw std::runtime_error("failed to capture private dbus-daemon address/pid");
    }

    auto trim = [](std::string value) {
        while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
            value.pop_back();
        }
        return value;
    };

    const std::string first = trim(firstBuffer);
    const std::string second = trim(secondBuffer);

    if (first.rfind("unix:", 0) == 0) {
        address_ = first;
        pid_ = static_cast<pid_t>(std::stoi(second));
    } else if (second.rfind("unix:", 0) == 0) {
        address_ = second;
        pid_ = static_cast<pid_t>(std::stoi(first));
    } else {
        throw std::runtime_error("failed to parse private dbus-daemon address/pid output");
    }
}

PrivateBus::~PrivateBus()
{
    if (pid_ > 0) {
        ::kill(pid_, SIGTERM);
        (void)::waitpid(pid_, nullptr, 0);
    }
}

ChildProcess::ChildProcess(pid_t pid, std::string logPath)
    : pid_(pid)
    , logPath_(std::move(logPath))
{
}

ChildProcess::~ChildProcess()
{
    if (pid_ > 0) {
        terminate();
        (void)waitForExit();
    }
}

void ChildProcess::terminate(int signalNumber)
{
    if (pid_ > 0) {
        ::kill(pid_, signalNumber);
    }
}

int ChildProcess::waitForExit()
{
    if (pid_ <= 0) {
        return 0;
    }
    int status = 0;
    if (::waitpid(pid_, &status, 0) < 0) {
        throw std::runtime_error("waitpid failed");
    }
    pid_ = -1;
    return status;
}

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool waitFor(const std::function<bool()>& predicate,
             std::chrono::milliseconds timeout,
             std::chrono::milliseconds pollInterval)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(pollInterval);
    }
    return predicate();
}

void writeFile(const std::string& path, const std::string& contents)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    expect(static_cast<bool>(out), "failed to open file for write: " + path);
    out << contents;
    expect(static_cast<bool>(out), "failed to write file: " + path);
}

std::string readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    expect(static_cast<bool>(in), "failed to open file for read: " + path);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

void replaceFile(const std::string& path, const std::string& contents)
{
    const std::string tempPath = path + ".tmp";
    writeFile(tempPath, contents);
    expect(::rename(tempPath.c_str(), path.c_str()) == 0, "rename replacement failed for " + path);
}

std::string shellQuote(const std::string& value)
{
    std::string quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back('\'');
    for (const char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

} // namespace integration_support
