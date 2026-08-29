//
// Created by vvass on 21-Jul-26.
//
#include "ObservationService.h"
#include "TransportFactory.h"
#include "ModelConfig.h"
#include <DaemonSignals.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <memory>

DEFINE_string(transport, "dbus", "Transport type: dbus, stdout");
DEFINE_string(transport_config, "system", "Transport-specific config (e.g. D-Bus bus type)");

int main(int argc, char* argv[])
{
    google::InitGoogleLogging(argv[0]);
    gflags::SetUsageMessage("Network Observation Daemon");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    RSCGroup::daemon_support::installSignalHandlers();

    RSCGroup::ModelConfig config;

    auto transport = RSCGroup::createTransport(FLAGS_transport, FLAGS_transport_config);
    if (!transport) {
        LOG(ERROR) << "Unknown transport type: " << FLAGS_transport;
        return 1;
    }
    auto adapter = std::make_unique<RSCGroup::NetworkObservationAdapter>(std::move(config));
    RSCGroup::ObservationService service(std::move(adapter), std::move(transport));

    if (!service.start()) {
        LOG(ERROR) << "Failed to start observation service";
        return 1;
    }

    LOG(INFO) << "network-observationd started";

    RSCGroup::daemon_support::waitForShutdown();

    LOG(INFO) << "Shutting down...";
    service.stop();
    google::ShutdownGoogleLogging();
    return 0;
}