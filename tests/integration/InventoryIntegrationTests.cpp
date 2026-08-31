#include "IntegrationSupport.h"

#include <InventoryClient.h>
#include <ClientResult.hpp>
#include <inventory.hpp>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using RSCGroup::InventoryClient;
namespace inventory = interop_contract::inventory;
using integration_support::ChildProcess;
using integration_support::PrivateBus;
using integration_support::TempDir;
using integration_support::expect;
using integration_support::readFile;
using integration_support::replaceFile;
using integration_support::waitFor;
using integration_support::writeFile;

ChildProcess spawnInventoryDaemon(const std::string& daemonPath,
                                  const std::string& busAddress,
                                  const TempDir& sandbox)
{
    const std::string logPath = sandbox.path() + "/inventory-agentd.log";
    const pid_t pid = ::fork();
    expect(pid >= 0, "fork for inventory-agentd failed");
    if (pid == 0) {
        ::setenv("DBUS_SESSION_BUS_ADDRESS", busAddress.c_str(), 1);
        FILE* log = std::fopen(logPath.c_str(), "a");
        if (!log) {
            _exit(127);
        }
        ::dup2(fileno(log), STDOUT_FILENO);
        ::dup2(fileno(log), STDERR_FILENO);
        std::fclose(log);

        const std::string infoDir = sandbox.path() + "/info";
        const std::string rmcDir = sandbox.path() + "/rmc";
        std::filesystem::create_directories(infoDir);
        std::filesystem::create_directories(rmcDir);

        const std::string deviceMeta = infoDir + "/device-meta.json";
        const std::string nodeName = infoDir + "/node-name";
        const std::string firmware = rmcDir + "/firmware";
        const std::string uuid = rmcDir + "/uuid";
        const std::string software = rmcDir + "/software";

        std::vector<std::string> argsStorage = {
            daemonPath,
            "--transport=dbus",
            "--transport_config=session",
            "--reconcile_ms=200",
            "--min_refresh_ms=50",
            "--device_meta_path=" + deviceMeta,
            "--node_name_path=" + nodeName,
            "--firmware_path=" + firmware,
            "--uuid_path=" + uuid,
            "--software_path=" + software,
        };
        std::vector<char*> argv;
        argv.reserve(argsStorage.size() + 1);
        for (auto& arg : argsStorage) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);
        ::execv(daemonPath.c_str(), argv.data());
        _exit(127);
    }
    return ChildProcess(pid, logPath);
}

int findEvent(const std::vector<std::string>& events, const std::string& expected)
{
    for (std::size_t index = 0; index < events.size(); ++index) {
        if (events[index] == expected) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void testInventoryDaemonLifecycle(const std::string& daemonPath)
{
    TempDir sandbox;
    std::filesystem::create_directories(sandbox.path() + "/info");
    std::filesystem::create_directories(sandbox.path() + "/rmc");
    writeFile(sandbox.path() + "/info/device-meta.json",
              "{\"device_class\":\"compute-node\",\"device_model_id\":\"711\",\"device_project\":\"ocean2\"}\n");
    writeFile(sandbox.path() + "/info/node-name", "rack12-node7\n");
    writeFile(sandbox.path() + "/rmc/software", "1.0.21\n");

    PrivateBus bus(sandbox.path() + "/session-bus.sock");
    ChildProcess daemon = spawnInventoryDaemon(daemonPath, bus.address(), sandbox);
    ::setenv("DBUS_SESSION_BUS_ADDRESS", bus.address().c_str(), 1);

    InventoryClient client("session");
    expect(waitFor([&] {
        const auto ready = client.tryGetReady();
        return ready.hasValue() || ready.error().code != interop_contract::ClientErrorCode::service_unavailable;
    }, std::chrono::seconds(10)), "inventory service did not become queryable\n" + readFile(daemon.logPath()));

    auto ready = client.tryGetReady();
    expect(ready.hasValue() && !ready.value(), "inventory service should start unready");
    const auto phase = client.tryGetPhase();
    expect(phase.hasValue() && phase.value() == std::string(inventory::PHASE_INITIALIZING),
           "inventory service should report initializing phase");

    const auto issues = client.tryGetIssues();
    expect(issues.hasValue(), "inventory GetIssues should succeed");
    expect(issues.value().contains("firmware-file"), "missing required firmware issue");
    expect(issues.value().contains("uuid-file"), "missing required uuid issue");

    std::mutex eventsMutex;
    std::vector<std::string> events;
    client.onInventoryChanged([&](const std::string& field) {
        std::scoped_lock lock(eventsMutex);
        events.push_back("inventory:" + field);
    });
    client.onSourceStateChanged([&](const std::string& source) {
        std::scoped_lock lock(eventsMutex);
        events.push_back("state:" + source);
    });
    client.onReadyChanged([&](bool value) {
        std::scoped_lock lock(eventsMutex);
        events.push_back(std::string("ready:") + (value ? "true" : "false"));
    });

    writeFile(sandbox.path() + "/rmc/firmware", "2.7.1\n");
    writeFile(sandbox.path() + "/rmc/uuid", "1234-5678\n");

    expect(waitFor([&] {
        const auto result = client.tryWaitReady(std::chrono::milliseconds(200));
        return result.hasValue() && result.value();
    }, std::chrono::seconds(10)), "inventory service did not become ready\n" + readFile(daemon.logPath()));

    const auto identity = client.tryGetIdentity();
    expect(identity.hasValue(), "inventory identity query should succeed");
    expect(identity.value().ready, "inventory snapshot should be ready");
    expect(identity.value().version > 0, "inventory snapshot version should advance");
    expect(std::get<std::string>(identity.value().fields.at(std::string(inventory::FIELD_FIRMWARE_VERSION))) == "2.7.1",
           "inventory snapshot should expose firmware");

    {
        std::scoped_lock lock(eventsMutex);
        const int readyIndex = findEvent(events, "ready:true");
        const int firmwareInventory = findEvent(events, "inventory:firmwareVersion");
        const int uuidInventory = findEvent(events, "inventory:uuid");
        const int firmwareState = findEvent(events, "state:firmware-file");
        const int uuidState = findEvent(events, "state:uuid-file");
        expect(readyIndex > firmwareInventory, "ReadyChanged(true) should follow firmware publication");
        expect(readyIndex > uuidInventory, "ReadyChanged(true) should follow uuid publication");
        expect(readyIndex > firmwareState, "ReadyChanged(true) should follow firmware source-state change");
        expect(readyIndex > uuidState, "ReadyChanged(true) should follow uuid source-state change");
    }

    const std::uint64_t versionBeforeRename = client.tryGetVersion().value();
    replaceFile(sandbox.path() + "/info/node-name", "rack12-node8\n");
    expect(waitFor([&] {
        const auto updated = client.tryGetIdentity();
        return updated.hasValue() &&
               std::get<std::string>(updated.value().fields.at(std::string(inventory::FIELD_NODE_NAME))) == "rack12-node8" &&
               updated.value().version > versionBeforeRename;
    }, std::chrono::seconds(10)), "rename-replace node-name did not propagate");

    std::size_t inventoryEventCount = 0;
    {
        std::scoped_lock lock(eventsMutex);
        inventoryEventCount = events.size();
    }
    expect(::unlink((sandbox.path() + "/rmc/firmware").c_str()) == 0, "failed to remove firmware file");
    expect(waitFor([&] {
        const auto states = client.tryGetSourceStates();
        return states.hasValue() &&
               states.value().at("firmware-file").health == inventory::SourceHealth::FAILED &&
               client.tryGetIssues().value().contains("firmware-file");
    }, std::chrono::seconds(10)), "firmware removal did not surface failed source state");

    const auto retainedFirmware = client.tryGetField(std::string(inventory::FIELD_FIRMWARE_VERSION));
    expect(retainedFirmware.hasValue(), "firmware field query should succeed after source removal");
    expect(std::get<std::string>(retainedFirmware.value().at(std::string(inventory::FIELD_FIRMWARE_VERSION))) == "2.7.1",
           "firmware removal should retain last-known-good value");

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    {
        std::scoped_lock lock(eventsMutex);
        expect(events.size() == inventoryEventCount + 1 || events.size() == inventoryEventCount + 2,
               "firmware removal should not trigger unrelated event storms");
    }

    replaceFile(sandbox.path() + "/rmc/firmware", "2.7.2\n");
    expect(waitFor([&] {
        const auto fields = client.tryGetField(std::string(inventory::FIELD_FIRMWARE_VERSION));
        const auto states = client.tryGetSourceStates();
        return fields.hasValue() &&
               states.hasValue() &&
               std::get<std::string>(fields.value().at(std::string(inventory::FIELD_FIRMWARE_VERSION))) == "2.7.2" &&
               states.value().at("firmware-file").health == inventory::SourceHealth::OK;
    }, std::chrono::seconds(10)), "firmware restore did not propagate");

    daemon.terminate(SIGTERM);
    const int status = daemon.waitForExit();
    expect(WIFEXITED(status) || WIFSIGNALED(status), "inventory daemon did not exit cleanly");

    expect(waitFor([&] {
        const auto stopped = client.tryGetReady();
        return !stopped.hasValue() &&
               stopped.error().code == interop_contract::ClientErrorCode::service_unavailable;
    }, std::chrono::seconds(5)), "inventory client did not surface service_unavailable after shutdown");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        expect(argc == 2, "inventory integration test requires inventory-agentd path argument");
        testInventoryDaemonLifecycle(argv[1]);
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
