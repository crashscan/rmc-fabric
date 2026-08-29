#include <DefaultInventoryManager.h>
#include <DeviceMetaFileSource.h>
#include <ScalarFileSource.h>
#include "TransportFactory.h"

#include "InventoryService.h"

#include <inventory.hpp>

#include <DaemonSignals.h>

#include <dbus-cxx.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include <chrono>
#include <memory>

DEFINE_string(transport, "dbus",
    "Transport: dbus, stdout");
DEFINE_int32(reconcile_ms, 60000,
    "Periodic full-refresh interval in milliseconds");
DEFINE_int32(min_refresh_ms, 1000,
    "Minimum interval between forced refreshes (coalescing window)");

// Source paths (overridable for sandbox testing)
DEFINE_string(device_meta_path, "/data/info/device-meta.json", "Device metadata JSON");
DEFINE_string(node_name_path, "/data/info/node-name",          "Node name file");
DEFINE_string(firmware_path,  "/etc/rmc/firmware",             "Firmware version file");
DEFINE_string(uuid_path,      "/etc/rmc/uuid",                 "Device UUID file");
DEFINE_string(software_path,  "/etc/rmc/software",             "Software version file");

int main(int argc, char* argv[])
{
    google::InitGoogleLogging(argv[0]);
    gflags::SetUsageMessage("Inventory Agent Daemon");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    RSCGroup::daemon_support::installSignalHandlers();

    using namespace RSCGroup;
    using namespace interop_contract::inventory;

    auto manager = std::make_shared<DefaultInventoryManager>();

    InventoryService::Settings settings;
    settings.reconcileInterval  = std::chrono::milliseconds(FLAGS_reconcile_ms);
    settings.minRefreshInterval = std::chrono::milliseconds(FLAGS_min_refresh_ms);

    InventoryService service(manager, settings);

    try {
        service.addSource(std::make_shared<ScalarFileSource>(
            "node-name-file", /*required=*/false,
            FLAGS_node_name_path, std::string(FIELD_NODE_NAME)));
        service.addSource(std::make_shared<DeviceMetaFileSource>( FLAGS_device_meta_path, /*required=*/false));

        // Mandatory-by-design, non-fatal: gate readiness, never abort startup.
        service.addSource(std::make_shared<ScalarFileSource>(
            "firmware-file", /*required=*/true,
            FLAGS_firmware_path, std::string(FIELD_FIRMWARE_VERSION)));
        service.addSource(std::make_shared<ScalarFileSource>(
            "uuid-file", /*required=*/true,
            FLAGS_uuid_path, std::string(FIELD_UUID)));

        service.addSource(std::make_shared<ScalarFileSource>(
            "software-file", /*required=*/false,
            FLAGS_software_path, std::string(FIELD_SOFTWARE_VERSION)));
    } catch (const std::exception& e) {
        LOG(ERROR) << "Source registration failed: " << e.what();
        return 1;
    }

    std::shared_ptr<DBus::StandaloneDispatcher> dispatcher;
    std::shared_ptr<DBus::Connection> connection;
    if (FLAGS_transport == "dbus") {
        dispatcher = DBus::StandaloneDispatcher::create();
        connection = dispatcher->create_connection(DBus::BusType::SYSTEM);
    }

    auto transport = TransportFactory::create(FLAGS_transport, connection);
    if (!transport) {
        return 1;
    }
    service.addTransport(transport);

    try {
        service.start();
    } catch (const std::exception& e) {
        LOG(ERROR) << "Failed to start inventory-agentd: " << e.what();
        return 1;
    }

    LOG(INFO) << "inventory-agentd started";

    RSCGroup::daemon_support::waitForShutdown();

    LOG(INFO) << "Shutting down...";
    service.stop();
    google::ShutdownGoogleLogging();
    return 0;
}
