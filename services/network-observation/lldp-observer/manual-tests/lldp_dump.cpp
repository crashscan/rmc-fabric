//
// lldp_dump.cpp — manual test: dump LLDP observations + snapshot
//
#include "LldpdSource.h"
#include "LldpObserver.h"
#include <lldpctl.hpp>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

int main()
{
    using namespace RSCGroup;
    using namespace std::chrono_literals;

    // --- Phase 1: snapshot via lldpctl API ---
    std::cout << "=== PHASE 1: Snapshot via lldpcli::LldpCtl ===\n";

    try {
        lldpcli::LldpCtl ctl;
        for (const auto& iface : ctl.GetInterfaces()) {
            auto name = iface.GetValue<std::string>(lldpctl_k_interface_name);
            std::cout << "Interface: " << (name ? *name : "?") << "\n";

            auto port = iface.GetPort();
            auto neighbors = port.GetAtomList(lldpctl_k_port_neighbors);

            std::cout << "  Neighbor count: " << neighbors.size() << "\n";

            for (const auto& nb : neighbors) {
                // Test what each key actually returns
                auto chassisIdStr = nb.GetValue<std::string>(lldpctl_k_chassis_id);
                auto portIdStr    = nb.GetValue<std::string>(lldpctl_k_port_id);
                auto chassisName  = nb.GetValue<std::string>(lldpctl_k_chassis_name);
                auto portDescr    = nb.GetValue<std::string>(lldpctl_k_port_descr);

                std::cout << "  Neighbor:\n";
                std::cout << "    chassis_id  (str): " << (chassisIdStr ? *chassisIdStr : "NULL") << "\n";
                std::cout << "    port_id     (str): " << (portIdStr ? *portIdStr : "NULL") << "\n";
                std::cout << "    chassis_name(str): " << (chassisName ? *chassisName : "NULL") << "\n";
                std::cout << "    port_descr  (str): " << (portDescr ? *portDescr : "NULL") << "\n";

                // Also try buffer type
                auto chassisIdBuf = nb.GetValue<std::vector<std::byte>>(lldpctl_k_chassis_id);
                auto portIdBuf    = nb.GetValue<std::vector<std::byte>>(lldpctl_k_port_id);

                std::cout << "    chassis_id  (buf): len=" << chassisIdBuf.size();
                if (!chassisIdBuf.empty()) {
                    std::cout << " [";
                    for (auto b : chassisIdBuf)
                        std::cout << std::hex << static_cast<int>(b) << " ";
                    std::cout << "]" << std::dec;
                }
                std::cout << "\n";

                std::cout << "    port_id     (buf): len=" << portIdBuf.size();
                if (!portIdBuf.empty()) {
                    std::cout << " [";
                    for (auto b : portIdBuf)
                        std::cout << std::hex << static_cast<int>(b) << " ";
                    std::cout << "]" << std::dec;
                }
                std::cout << "\n\n";
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Snapshot failed: " << e.what() << "\n";
    }

    // --- Phase 2: live watch via LldpdSource ---
    std::cout << "=== PHASE 2: Live watch via LldpdSource (30 seconds) ===\n";

    auto source = std::make_unique<LldpdSource>(
        LldpSourceConfig{},
        [](const LldpObservation& obs) {
            std::cout << "LLDP: " << obs.localIfname
                      << " " << (obs.event == ObservationEvent::Present ? "PRESENT" : "REMOVED")
                      << " chassis_id=\""
                      << (obs.remoteChassisId ? *obs.remoteChassisId : "")
                      << "\" port_id=\""
                      << (obs.remotePortId ? *obs.remotePortId : "")
                      << "\" name=\""
                      << (obs.remoteSystemName ? *obs.remoteSystemName : "")
                      << "\"\n";
        });

    auto observer = std::make_unique<LldpObserver>(std::move(source));

    if (!observer->start()) {
        std::cerr << "LLDP observer start failed\n";
        return 1;
    }

    std::cout << "Observer started, waiting 30s for events...\n";
    std::this_thread::sleep_for(30s);

    observer->stop();
    std::cout << "Observer stopped.\n";

    return 0;
}