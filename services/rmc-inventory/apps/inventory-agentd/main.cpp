#include <DefaultInventoryManager.h>
#include <DeviceMetaFileSource.h>
#include <ScalarFileSource.h>
#include "TransportFactory.h"

#include "InventoryService.h"

#include <inventory.hpp>

#include <GflagsConfig.h>
#include <DaemonRunner.h>

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
    gflags::SetUsageMessage("Inventory Agent Daemon");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    using namespace RSCGroup;
    using namespace interop_contract::inventory;

    GflagsConfig cfg;

    auto manager = std::make_shared<DefaultInventoryManager>();

    InventoryService::Settings settings;
    settings.reconcileInterval  = std::chrono::milliseconds(cfg.getInt("reconcile_ms", 60000));
    settings.minRefreshInterval = std::chrono::milliseconds(cfg.getInt("min_refresh_ms", 1000));

    InventoryService service(manager, settings);

    try {
        service.addSource(std::make_shared<ScalarFileSource>(
            "node-name-file", /*required=*/false,
            cfg.getString("node_name_path", "/data/info/node-name"),
            std::string(FIELD_NODE_NAME)));
        service.addSource(std::make_shared<DeviceMetaFileSource>(
            cfg.getString("device_meta_path", "/data/info/device-meta.json"),
            /*required=*/false));

        // Mandatory-by-design, non-fatal: gate readiness, never abort startup.
        service.addSource(std::make_shared<ScalarFileSource>(
            "firmware-file", /*required=*/true,
            cfg.getString("firmware_path", "/etc/rmc/firmware"),
            std::string(FIELD_FIRMWARE_VERSION)));
        service.addSource(std::make_shared<ScalarFileSource>(
            "uuid-file", /*required=*/true,
            cfg.getString("uuid_path", "/etc/rmc/uuid"),
            std::string(FIELD_UUID)));
        service.addSource(std::make_shared<ScalarFileSource>(
            "software-file", /*required=*/false,
            cfg.getString("software_path", "/etc/rmc/software"),
            std::string(FIELD_SOFTWARE_VERSION)));
    } catch (const std::exception& e) {
        LOG(ERROR) << "Source registration failed: " << e.what();
        return 1;
    }

    const std::string transportName = cfg.getString("transport", "dbus");

    std::shared_ptr<DBus::StandaloneDispatcher> dispatcher;
    std::shared_ptr<DBus::Connection> connection;
    if (transportName == "dbus") {
        dispatcher = DBus::StandaloneDispatcher::create();
        connection = dispatcher->create_connection(DBus::BusType::SYSTEM);
    }

    auto transport = TransportFactory::create(transportName, connection);
    if (!transport) {
        return 1;
    }
    service.addTransport(transport);

    return DaemonRunner::run(argv[0], service, cfg);
}

