#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <sys/types.h>

namespace integration_support {

class TempDir {
public:
    TempDir();
    ~TempDir();

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    [[nodiscard]] const std::string& path() const { return path_; }

private:
    std::string path_;
};

class PrivateBus {
public:
    explicit PrivateBus(const std::string& socketPath);
    ~PrivateBus();

    PrivateBus(const PrivateBus&) = delete;
    PrivateBus& operator=(const PrivateBus&) = delete;

    [[nodiscard]] const std::string& address() const { return address_; }

private:
    pid_t pid_{-1};
    std::string address_;
};

class ChildProcess {
public:
    ChildProcess(pid_t pid, std::string logPath);
    ~ChildProcess();

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    void terminate(int signalNumber = 15);
    [[nodiscard]] int waitForExit();
    [[nodiscard]] pid_t pid() const { return pid_; }
    [[nodiscard]] const std::string& logPath() const { return logPath_; }

private:
    pid_t pid_{-1};
    std::string logPath_;
};

void expect(bool condition, const std::string& message);
bool waitFor(const std::function<bool()>& predicate,
             std::chrono::milliseconds timeout,
             std::chrono::milliseconds pollInterval = std::chrono::milliseconds(20));
void writeFile(const std::string& path, const std::string& contents);
std::string readFile(const std::string& path);
void replaceFile(const std::string& path, const std::string& contents);
std::string shellQuote(const std::string& value);

} // namespace integration_support
