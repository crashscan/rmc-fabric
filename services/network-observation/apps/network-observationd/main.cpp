//
// Created by vvass on 21-Jul-26.
//
#include "ObservationService.h"
#include "TransportFactory.h"
#include "ModelConfig.h"
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <csignal>
#include <memory>
#include <thread>

DEFINE_string(transport, "dbus", "Transport type: dbus, stdout");
DEFINE_string(transport_config, "system", "Transport-specific config (e.g. D-Bus bus type)");

namespace {

volatile sig_atomic_t g_running = 1;

void handleSignal(int) {
    g_running = 0;
}

}

int main(int argc, char* argv[])
{
    google::InitGoogleLogging(argv[0]);
    gflags::SetUsageMessage("Network Observation Daemon");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);

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

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    LOG(INFO) << "Shutting down...";
    service.stop();
    google::ShutdownGoogleLogging();
    return 0;
}