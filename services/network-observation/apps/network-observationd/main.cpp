//
// Created by vvass on 21-Jul-26.
//
#include "ObservationService.h"
#include "NetlinkLldpObservationRuntime.h"
#include "ModelConfig.h"
#include "TransportFactory.h"
#include <GflagsConfig.h>
#include <DaemonRunner.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <memory>

DEFINE_string(transport, "dbus", "Transport type: dbus, stdout");
DEFINE_string(transport_config, "system", "Transport-specific config (e.g. D-Bus bus type)");

int main(int argc, char* argv[])
{
    gflags::SetUsageMessage("Network Observation Daemon");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    RSCGroup::GflagsConfig cfg;

    const std::string transportName   = cfg.getString("transport", "dbus");
    const std::string transportConfig = cfg.getString("transport_config", "system");

    auto transport = RSCGroup::createTransport(transportName, transportConfig);
    if (!transport) {
        // Cannot use LOG() yet — logging not initialised.
        // Use stderr directly so the error is visible.
        fprintf(stderr, "Unknown transport type: %s\n", transportName.c_str());
        return 1;
    }
    RSCGroup::ModelConfig modelConfig;
    auto runtime = std::make_unique<RSCGroup::NetlinkLldpObservationRuntime>(std::move(modelConfig));
    RSCGroup::ObservationService service(std::move(runtime), std::move(transport));

    return RSCGroup::DaemonRunner::run(argv[0], service, cfg);
}
