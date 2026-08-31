#include "IntegrationSupport.h"

#include <DbusClient.h>
#include <ObservationService.h>
#include <DbusTransport.h>
#include <dbus-cxx.h>
#include <jsoncpp/json/reader.h>
#include <jsoncpp/json/writer.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace {

using integration_support::ChildProcess;
using integration_support::PrivateBus;
using integration_support::TempDir;
using integration_support::expect;
using integration_support::readFile;
using integration_support::waitFor;
namespace contract = interop_contract::network_observation;

RSCGroup::LocalInterfaceState interfaceFromJson(const Json::Value& value)
{
    RSCGroup::LocalInterfaceState iface;
    iface.ifindex = value["ifindex"].asInt();
    iface.ifname = value["ifname"].asString();
    iface.mac = value["mac"].asString();
    iface.adminUp = value["adminUp"].asBool();
    iface.running = value["running"].asBool();
    iface.operstate = value["operstate"].asString();
    if (value.isMember("master")) {
        iface.masterIfname = value["master"].asString();
    }
    for (const auto& entry : value["ipv4"]) {
        iface.ipv4.insert(entry.asString());
    }
    for (const auto& entry : value["ipv6"]) {
        iface.ipv6.insert(entry.asString());
    }
    return iface;
}

RSCGroup::RemoteCandidate candidateFromJson(const Json::Value& value)
{
    RSCGroup::RemoteCandidate candidate;
    candidate.mac = value["mac"].asString();
    candidate.classification = contract::classificationFromString(value["classification"].asString());
    candidate.status = contract::statusFromString(value["status"].asString());
    candidate.seenInFdb = value["seenInFdb"].asBool();
    candidate.seenInNeigh = value["seenInNeigh"].asBool();
    candidate.seenInLldp = value["seenInLldp"].asBool();
    if (value.isMember("bridgePort")) {
        candidate.bridgePort = value["bridgePort"].asString();
    }
    if (value.isMember("remoteChassisId")) {
        candidate.remoteChassisId = value["remoteChassisId"].asString();
    }
    if (value.isMember("remotePortId")) {
        candidate.remotePortId = value["remotePortId"].asString();
    }
    if (value.isMember("remoteSystemName")) {
        candidate.remoteSystemName = value["remoteSystemName"].asString();
    }
    for (const auto& entry : value["neighborIfaces"]) {
        candidate.neighborIfaces.insert(entry.asString());
    }
    for (const auto& entry : value["ipv4"]) {
        candidate.ipv4.insert(entry.asString());
    }
    for (const auto& entry : value["ipv6"]) {
        candidate.ipv6.insert(entry.asString());
    }
    return candidate;
}

class PipeControlledRuntime final : public RSCGroup::IObservationRuntime {
public:
    explicit PipeControlledRuntime(int readFd)
        : readFd_(readFd)
    {
    }

    ~PipeControlledRuntime() override
    {
        stop();
    }

    void setEventSink(RSCGroup::IModelEventSink* sink) override
    {
        sink_ = sink;
    }

    void setInterfacePolicy(std::unique_ptr<RSCGroup::IInterfacePolicy>) override {}
    void setClassifier(std::unique_ptr<RSCGroup::ICandidateClassifier>) override {}

    bool start() override
    {
        running_ = true;
        worker_ = std::thread([this] { run(); });
        return true;
    }

    void stop() override
    {
        running_.exchange(false);
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    bool isRunning() const override
    {
        return running_.load();
    }

    RSCGroup::LocalNetworkSnapshot localSnapshot() const override
    {
        std::scoped_lock lock(mutex_);
        return snapshot_;
    }

    std::vector<RSCGroup::RemoteCandidate> remoteCandidates() const override
    {
        std::scoped_lock lock(mutex_);
        std::vector<RSCGroup::RemoteCandidate> out;
        out.reserve(candidates_.size());
        for (const auto& [_, candidate] : candidates_) {
            out.push_back(candidate);
        }
        return out;
    }

    std::optional<RSCGroup::RemoteCandidate> findCandidateByMac(const std::string& mac) const override
    {
        std::scoped_lock lock(mutex_);
        const auto it = candidates_.find(mac);
        if (it == candidates_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    void age(std::chrono::steady_clock::time_point) override {}

private:
    void emit(RSCGroup::ModelEventKind kind,
              std::optional<std::string> ifname = std::nullopt,
              std::optional<std::string> mac = std::nullopt)
    {
        if (!sink_) {
            return;
        }
        RSCGroup::ModelEvent event;
        event.kind = kind;
        event.timestamp = std::chrono::steady_clock::now();
        event.ifname = std::move(ifname);
        event.mac = std::move(mac);
        sink_->onModelEvent(event);
    }

    void run()
    {
        FILE* input = ::fdopen(readFd_, "r");
        if (!input) {
            if (readFd_ >= 0) {
                ::close(readFd_);
                readFd_ = -1;
            }
            running_ = false;
            return;
        }
        char* line = nullptr;
        size_t lineCap = 0;
        Json::CharReaderBuilder builder;
        while (running_.load()) {
            const ssize_t bytes = ::getline(&line, &lineCap, input);
            if (bytes <= 0) {
                break;
            }
            Json::Value command;
            std::string errors;
            std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
            const char* begin = line;
            const char* end = line + bytes;
            if (!reader->parse(begin, end, &command, &errors)) {
                continue;
            }
            const std::string type = command["type"].asString();
            if (type == "shutdown") {
                break;
            }
            if (type == "set_interface") {
                auto iface = interfaceFromJson(command["interface"]);
                {
                    std::scoped_lock lock(mutex_);
                    snapshot_.interfaces[iface.ifname] = iface;
                }
                emit(RSCGroup::ModelEventKind::LocalInterfaceChanged, iface.ifname);
                continue;
            }
            if (type == "remove_interface") {
                const auto ifname = command["ifname"].asString();
                {
                    std::scoped_lock lock(mutex_);
                    snapshot_.interfaces.erase(ifname);
                }
                emit(RSCGroup::ModelEventKind::LocalInterfaceRemoved, ifname);
                continue;
            }
            if (type == "set_candidate") {
                auto candidate = candidateFromJson(command["candidate"]);
                const bool existed = [&] {
                    std::scoped_lock lock(mutex_);
                    const bool wasPresent = candidates_.contains(candidate.mac);
                    candidates_[candidate.mac] = candidate;
                    return wasPresent;
                }();
                emit(existed ? RSCGroup::ModelEventKind::CandidateUpdated
                             : RSCGroup::ModelEventKind::CandidateAdded,
                     std::nullopt, candidate.mac);
                continue;
            }
            if (type == "remove_candidate") {
                const auto mac = command["mac"].asString();
                {
                    std::scoped_lock lock(mutex_);
                    candidates_.erase(mac);
                }
                emit(RSCGroup::ModelEventKind::CandidateRemoved, std::nullopt, mac);
            }
        }
        std::free(line);
        ::fclose(input);
        readFd_ = -1;
        running_ = false;
    }

    int readFd_{-1};
    RSCGroup::IModelEventSink* sink_{nullptr};
    mutable std::mutex mutex_;
    RSCGroup::LocalNetworkSnapshot snapshot_;
    std::unordered_map<std::string, RSCGroup::RemoteCandidate> candidates_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

ChildProcess spawnObservationService(const std::string& busAddress,
                                     const std::string& logPath,
                                     int readFd)
{
    const pid_t pid = ::fork();
    expect(pid >= 0, "fork for observation service failed");
    if (pid == 0) {
        ::setenv("DBUS_SESSION_BUS_ADDRESS", busAddress.c_str(), 1);
        FILE* log = std::fopen(logPath.c_str(), "a");
        if (!log) {
            _exit(127);
        }
        ::dup2(fileno(log), STDOUT_FILENO);
        ::dup2(fileno(log), STDERR_FILENO);
        std::fclose(log);

        auto runtime = std::make_unique<PipeControlledRuntime>(readFd);
        auto* runtimePtr = runtime.get();
        auto transport = std::make_shared<RSCGroup::DbusTransport>("session");
        RSCGroup::ObservationService service(std::move(runtime), transport, std::chrono::hours(1));
        if (!service.start()) {
            _exit(2);
        }

        while (::kill(getppid(), 0) == 0 && runtimePtr->isRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        service.stop();
        _exit(0);
    }
    ::close(readFd);
    return ChildProcess(pid, logPath);
}

ChildProcess spawnMalformedObservationService(const std::string& busAddress,
                                              const std::string& logPath,
                                              int readFd)
{
    const pid_t pid = ::fork();
    expect(pid >= 0, "fork for malformed observation service failed");
    if (pid == 0) {
        ::setenv("DBUS_SESSION_BUS_ADDRESS", busAddress.c_str(), 1);
        FILE* log = std::fopen(logPath.c_str(), "a");
        if (!log) {
            _exit(127);
        }
        ::dup2(fileno(log), STDOUT_FILENO);
        ::dup2(fileno(log), STDERR_FILENO);
        std::fclose(log);

        auto dispatcher = DBus::StandaloneDispatcher::create();
        auto connection = dispatcher->create_connection(DBus::BusType::SESSION);
        const auto nameResult = connection->request_name(std::string(contract::SERVICE_NAME));
        if (nameResult != DBus::RequestNameResponse::PrimaryOwner &&
            nameResult != DBus::RequestNameResponse::AlreadyOwner) {
            _exit(3);
        }

        auto object = connection->create_object(std::string(contract::OBJECT_PATH));
        connection->register_object(object);
        object->create_method<std::map<std::string, DBus::Variant>()>(
            std::string(contract::INTERFACE),
            std::string(contract::METHOD_GET_LOCAL_SNAPSHOT),
            [] { return std::map<std::string, DBus::Variant>{}; });
        object->create_method<std::map<std::string, DBus::Variant>(std::string)>(
            std::string(contract::INTERFACE),
            std::string(contract::METHOD_GET_INTERFACE),
            [](std::string) {
                return std::map<std::string, DBus::Variant>{
                    {std::string(contract::K_IFINDEX), DBus::Variant(std::string("bad-type"))},
                    {std::string(contract::K_IFNAME), DBus::Variant(std::string("eth0"))},
                    {std::string(contract::K_MAC), DBus::Variant(std::string("aa:bb:cc:dd:ee:ff"))},
                    {std::string(contract::K_ADMINUP), DBus::Variant(true)},
                    {std::string(contract::K_RUNNING), DBus::Variant(true)},
                    {std::string(contract::K_OPERSTATE), DBus::Variant(std::string("UP"))},
                    {std::string(contract::K_IPV4), DBus::Variant(std::vector<DBus::Variant>{})},
                    {std::string(contract::K_IPV6), DBus::Variant(std::vector<DBus::Variant>{})},
                };
            });
        object->create_method<std::vector<std::string>()>(
            std::string(contract::INTERFACE),
            std::string(contract::METHOD_GET_REMOTE_CANDIDATE_MACS),
            [] { return std::vector<std::string>{}; });
        object->create_method<std::map<std::string, DBus::Variant>(std::string)>(
            std::string(contract::INTERFACE),
            std::string(contract::METHOD_GET_CANDIDATE_BY_MAC),
            [](std::string) { return std::map<std::string, DBus::Variant>{}; });
        object->create_method<bool()>(
            std::string(contract::INTERFACE),
            std::string(contract::METHOD_GET_READY),
            [] { return true; });
        object->create_method<std::string()>(
            std::string(contract::INTERFACE),
            std::string(contract::METHOD_GET_PHASE),
            [] { return std::string(contract::PHASE_LIVE); });

        FILE* input = ::fdopen(readFd, "r");
        if (!input) {
            _exit(4);
        }
        char buffer[8];
        (void)::fread(buffer, 1, sizeof(buffer), input);
        ::fclose(input);
        connection->unregister_object(std::string(contract::OBJECT_PATH));
        _exit(0);
    }
    ::close(readFd);
    return ChildProcess(pid, logPath);
}

void sendCommand(int writeFd, const Json::Value& command)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    const std::string payload = Json::writeString(builder, command) + "\n";
    const ssize_t written = ::write(writeFd, payload.data(), payload.size());
    expect(written == static_cast<ssize_t>(payload.size()), "failed to send control command");
}

void testObservationServiceRoundTrip()
{
    TempDir sandbox;
    PrivateBus bus(sandbox.path() + "/session-bus.sock");
    int commandPipe[2];
    expect(::pipe(commandPipe) == 0, "failed to create observation command pipe");
    const std::string logPath = sandbox.path() + "/network-observationd.log";
    ChildProcess child = spawnObservationService(bus.address(), logPath, commandPipe[0]);
    ::setenv("DBUS_SESSION_BUS_ADDRESS", bus.address().c_str(), 1);

    RSCGroup::DbusClient client("session");
    expect(client.tryConnect().hasValue(), "observation client failed to create proxy");
    expect(waitFor([&] {
        const auto ready = client.tryGetReady();
        return ready.hasValue();
    }, std::chrono::seconds(10)), "observation client failed to reach service\n" + readFile(logPath));

    std::mutex eventsMutex;
    std::vector<std::string> events;
    client.onLocalStateChanged([&] {
        std::scoped_lock lock(eventsMutex);
        events.push_back("local");
    });
    client.onInterfaceChanged([&](const std::string& ifname) {
        std::scoped_lock lock(eventsMutex);
        events.push_back("iface+" + ifname);
    });
    client.onInterfaceRemoved([&](const std::string& ifname) {
        std::scoped_lock lock(eventsMutex);
        events.push_back("iface-" + ifname);
    });
    client.onCandidateChanged([&](const std::string& mac) {
        std::scoped_lock lock(eventsMutex);
        events.push_back("cand+" + mac);
    });
    client.onCandidateRemoved([&](const std::string& mac) {
        std::scoped_lock lock(eventsMutex);
        events.push_back("cand-" + mac);
    });

    const auto ready = client.tryGetReady();
    expect(ready.hasValue() && ready.value(), "observation service should start ready");
    const auto phase = client.tryGetPhase();
    expect(phase.hasValue() && phase.value() == std::string(contract::PHASE_LIVE),
           "observation service should report live phase");
    const auto issues = client.tryGetIssues();
    expect(issues.hasValue(), "observation GetIssues should succeed");

    Json::Value ifaceCommand(Json::objectValue);
    ifaceCommand["type"] = "set_interface";
    ifaceCommand["interface"]["ifindex"] = 7;
    ifaceCommand["interface"]["ifname"] = "eth0";
    ifaceCommand["interface"]["mac"] = "aa:bb:cc:dd:ee:ff";
    ifaceCommand["interface"]["adminUp"] = true;
    ifaceCommand["interface"]["running"] = true;
    ifaceCommand["interface"]["operstate"] = "UP";
    ifaceCommand["interface"]["master"] = "br0";
    ifaceCommand["interface"]["ipv4"].append("10.0.0.10/24");
    ifaceCommand["interface"]["ipv6"].append("fe80::10/64");
    sendCommand(commandPipe[1], ifaceCommand);

    expect(waitFor([&] {
        const auto iface = client.tryGetInterface("eth0");
        return iface.hasValue() &&
               iface.value().has_value() &&
               iface.value()->masterIfname.has_value() &&
               *iface.value()->masterIfname == "br0";
    }, std::chrono::seconds(10)), "interface update did not propagate");

    Json::Value candidateCommand(Json::objectValue);
    candidateCommand["type"] = "set_candidate";
    candidateCommand["candidate"]["mac"] = "00:11:22:33:44:55";
    candidateCommand["candidate"]["classification"] = "RemoteEndpoint";
    candidateCommand["candidate"]["status"] = "Confirmed";
    candidateCommand["candidate"]["seenInFdb"] = true;
    candidateCommand["candidate"]["seenInNeigh"] = true;
    candidateCommand["candidate"]["seenInLldp"] = true;
    candidateCommand["candidate"]["bridgePort"] = "swp1";
    candidateCommand["candidate"]["remoteChassisId"] = "chassis-1";
    candidateCommand["candidate"]["remotePortId"] = "port-7";
    candidateCommand["candidate"]["remoteSystemName"] = "leaf-7";
    candidateCommand["candidate"]["neighborIfaces"].append("eth0");
    candidateCommand["candidate"]["ipv4"].append("10.0.0.20/24");
    candidateCommand["candidate"]["ipv6"].append("fe80::20/64");
    sendCommand(commandPipe[1], candidateCommand);

    expect(waitFor([&] {
        const auto macs = client.tryGetRemoteCandidateMacs();
        const auto candidate = client.tryGetCandidateByMac("00:11:22:33:44:55");
        return macs.hasValue() &&
               !macs.value().empty() &&
               candidate.hasValue() &&
               candidate.value().has_value() &&
               candidate.value()->classification == contract::CandidateClassification::RemoteEndpoint &&
               candidate.value()->status == contract::CandidateStatus::Confirmed;
    }, std::chrono::seconds(10)), "candidate update did not propagate");

    Json::Value candidateUpdate = candidateCommand;
    candidateUpdate["candidate"]["status"] = "Aged";
    sendCommand(commandPipe[1], candidateUpdate);
    expect(waitFor([&] {
        const auto candidate = client.tryGetCandidateByMac("00:11:22:33:44:55");
        return candidate.hasValue() &&
               candidate.value().has_value() &&
               candidate.value()->status == contract::CandidateStatus::Aged;
    }, std::chrono::seconds(10)), "candidate status update did not propagate");

    Json::Value removeCandidate(Json::objectValue);
    removeCandidate["type"] = "remove_candidate";
    removeCandidate["mac"] = "00:11:22:33:44:55";
    sendCommand(commandPipe[1], removeCandidate);
    expect(waitFor([&] {
        const auto candidate = client.tryGetCandidateByMac("00:11:22:33:44:55");
        return candidate.hasValue() && !candidate.value().has_value();
    }, std::chrono::seconds(10)), "candidate removal did not propagate");

    Json::Value removeInterface(Json::objectValue);
    removeInterface["type"] = "remove_interface";
    removeInterface["ifname"] = "eth0";
    sendCommand(commandPipe[1], removeInterface);
    expect(waitFor([&] {
        const auto iface = client.tryGetInterface("eth0");
        return iface.hasValue() && !iface.value().has_value();
    }, std::chrono::seconds(10)), "interface removal did not propagate");

    {
        std::scoped_lock lock(eventsMutex);
        expect(std::find(events.begin(), events.end(), "local") != events.end(),
               "expected LocalStateChanged signal");
        expect(std::find(events.begin(), events.end(), "iface+eth0") != events.end(),
               "expected InterfaceChanged signal");
        expect(std::find(events.begin(), events.end(), "iface-eth0") != events.end(),
               "expected InterfaceRemoved signal");
        expect(std::find(events.begin(), events.end(), "cand+00:11:22:33:44:55") != events.end(),
               "expected CandidateChanged signal");
        expect(std::find(events.begin(), events.end(), "cand-00:11:22:33:44:55") != events.end(),
               "expected CandidateRemoved signal");
    }

    Json::Value shutdown(Json::objectValue);
    shutdown["type"] = "shutdown";
    sendCommand(commandPipe[1], shutdown);
    ::close(commandPipe[1]);
    const int status = child.waitForExit();
    expect(WIFEXITED(status), "observation service child should exit cleanly");

    expect(waitFor([&] {
        const auto after = client.tryGetLocalSnapshot();
        return !after.hasValue() &&
               after.error().code == interop_contract::ClientErrorCode::service_unavailable;
    }, std::chrono::seconds(5)), "observation client did not surface service_unavailable after shutdown");
}

void testObservationClientReconnectRequiresExplicitReconnect()
{
    TempDir sandbox;
    PrivateBus bus(sandbox.path() + "/session-bus.sock");
    int commandPipe[2];
    expect(::pipe(commandPipe) == 0, "failed to create observation reconnect command pipe");
    const std::string firstLogPath = sandbox.path() + "/network-observation-first.log";
    ChildProcess firstChild = spawnObservationService(bus.address(), firstLogPath, commandPipe[0]);
    ::setenv("DBUS_SESSION_BUS_ADDRESS", bus.address().c_str(), 1);

    RSCGroup::DbusClient client("session");
    expect(client.tryConnect().hasValue(), "observation reconnect client failed to connect");
    expect(waitFor([&] {
        const auto ready = client.tryGetReady();
        return ready.hasValue() && ready.value();
    }, std::chrono::seconds(10)), "first observation service did not become ready\n" + readFile(firstLogPath));

    Json::Value shutdown(Json::objectValue);
    shutdown["type"] = "shutdown";
    sendCommand(commandPipe[1], shutdown);
    ::close(commandPipe[1]);
    const int firstStatus = firstChild.waitForExit();
    expect(WIFEXITED(firstStatus), "first observation service should exit cleanly");

    expect(waitFor([&] {
        const auto result = client.tryGetLocalSnapshot();
        return !result.hasValue() &&
               result.error().code == interop_contract::ClientErrorCode::service_unavailable;
    }, std::chrono::seconds(5)), "observation client should surface service_unavailable before reconnect");

    int secondPipe[2];
    expect(::pipe(secondPipe) == 0, "failed to create second observation reconnect command pipe");
    const std::string secondLogPath = sandbox.path() + "/network-observation-second.log";
    ChildProcess secondChild = spawnObservationService(bus.address(), secondLogPath, secondPipe[0]);

    expect(client.tryConnect().hasValue(), "observation reconnect client failed to reconnect");
    expect(waitFor([&] {
        const auto readyAgain = client.tryGetReady();
        return readyAgain.hasValue() && readyAgain.value();
    }, std::chrono::seconds(10)), "second observation service did not become ready\n" + readFile(secondLogPath));

    const auto issues = client.tryGetIssues();
    expect(issues.hasValue(), "observation GetIssues should succeed after reconnect");

    sendCommand(secondPipe[1], shutdown);
    ::close(secondPipe[1]);
    const int secondStatus = secondChild.waitForExit();
    expect(WIFEXITED(secondStatus), "second observation service should exit cleanly");
}

void testObservationClientRejectsMalformedResponse()
{
    TempDir sandbox;
    PrivateBus bus(sandbox.path() + "/session-bus.sock");
    int controlPipe[2];
    expect(::pipe(controlPipe) == 0, "failed to create malformed service control pipe");
    const std::string logPath = sandbox.path() + "/network-observation-malformed.log";
    ChildProcess child = spawnMalformedObservationService(bus.address(), logPath, controlPipe[0]);
    ::setenv("DBUS_SESSION_BUS_ADDRESS", bus.address().c_str(), 1);

    RSCGroup::DbusClient client("session");
    expect(client.tryConnect().hasValue(), "malformed observation client failed to create proxy");
    expect(waitFor([&] {
        const auto ready = client.tryGetReady();
        return ready.hasValue();
    }, std::chrono::seconds(10)), "malformed observation service did not answer queries\n" + readFile(logPath));

    expect(waitFor([&] {
        const auto iface = client.tryGetInterface("eth0");
        return !iface.hasValue() &&
               iface.error().code == interop_contract::ClientErrorCode::decode_error;
    }, std::chrono::seconds(5)), "malformed observation response should surface decode_error");

    ::close(controlPipe[1]);
    const int status = child.waitForExit();
    expect(WIFEXITED(status), "malformed observation service should exit cleanly");
}

} // namespace

int main()
{
    try {
        testObservationServiceRoundTrip();
        testObservationClientReconnectRequiresExplicitReconnect();
        testObservationClientRejectsMalformedResponse();
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
