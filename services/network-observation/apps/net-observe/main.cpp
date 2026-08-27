//
// Created by vvass on 21-Jul-26.
//
#include "DbusClient.h"
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <json/writer.h>
#include <iostream>
#include <string>
#include <thread>

DEFINE_string(command, "status",
    "Command: status, remote, watch, monitor");
DEFINE_string(bus, "system",
    "D-Bus bus type: system or session");
DEFINE_string(interface, "",
    "Filter output by interface name");
DEFINE_string(mac, "",
    "Filter output by MAC address");
DEFINE_bool(pretty, true,
    "Pretty-print JSON output");
DEFINE_bool(lldp, false,
    "For status/monitor: include LLDP neighbor details per interface. "
    "For remote: show only LLDP-backed candidates.");

namespace {

// ---------------------------------------------------------------------------
// Local string helpers (mirror server-side serialization)
// ---------------------------------------------------------------------------

static std::string classificationToString(RSCGroup::CandidateClassification c)
{
    using enum RSCGroup::CandidateClassification;
    switch (c) {
        case Artifact:       return "Artifact";
        case LocalSelf:      return "LocalSelf";
        case WeakCandidate:  return "WeakCandidate";
        case ProbableEndpoint: return "ProbableEndpoint";
        case RemoteEndpoint: return "RemoteEndpoint";
        case GatewayLike:    return "GatewayLike";
        case TopologyPeer:   return "TopologyPeer";
        case Unknown:        return "Unknown";
    }
    return "Unknown";
}

static std::string statusToString(RSCGroup::CandidateStatus s)
{
    using enum RSCGroup::CandidateStatus;
    switch (s) {
        case Provisional: return "Provisional";
        case Confirmed:   return "Confirmed";
        case Aged:        return "Aged";
        case Expired:     return "Expired";
        case Removed:     return "Removed";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// JSON output helpers
// ---------------------------------------------------------------------------

void printJson(const Json::Value& root)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = FLAGS_pretty ? "  " : "";
    std::cout << Json::writeString(builder, root) << '\n';
}

Json::Value ifaceToJson(const RSCGroup::LocalInterfaceState& iface)
{
    Json::Value j;
    j["ifindex"] = iface.ifindex;
    j["ifname"] = iface.ifname;
    j["mac"] = iface.mac;
    j["adminUp"] = iface.adminUp;
    j["running"] = iface.running;
    j["operstate"] = iface.operstate;
    if (iface.masterIfname) {
        j["master"] = *iface.masterIfname;
    }

    Json::Value ipv4Arr;
    for (const auto& a : iface.ipv4) ipv4Arr.append(a);
    j["ipv4"] = ipv4Arr;

    Json::Value ipv6Arr;
    for (const auto& a : iface.ipv6) ipv6Arr.append(a);
    j["ipv6"] = ipv6Arr;

    return j;
}

Json::Value candidateToJson(const RSCGroup::RemoteCandidate& c)
{
    Json::Value j;
    j["mac"] = c.mac;
    j["classification"] = classificationToString(c.classification);
    j["status"] = statusToString(c.status);
    j["seenInFdb"] = c.seenInFdb;
    j["seenInNeigh"] = c.seenInNeigh;
    j["seenInLldp"] = c.seenInLldp;
    if (c.bridgePort) j["bridgePort"] = *c.bridgePort;
    if (c.remoteChassisId) j["remoteChassisId"] = *c.remoteChassisId;
    if (c.remotePortId) j["remotePortId"] = *c.remotePortId;
    if (c.remoteSystemName) j["remoteSystemName"] = *c.remoteSystemName;

    Json::Value ipv4Arr;
    for (const auto& ip : c.ipv4) ipv4Arr.append(ip);
    j["ipv4"] = ipv4Arr;

    Json::Value ipv6Arr;
    for (const auto& ip : c.ipv6) ipv6Arr.append(ip);
    j["ipv6"] = ipv6Arr;

    Json::Value neighArr;
    for (const auto& n : c.neighborIfaces) neighArr.append(n);
    j["neighborIfaces"] = neighArr;

    return j;
}

/**
 * @brief Fetch all remote candidates using two-phase fetch (MACs then per-MAC).
 */
static std::vector<RSCGroup::RemoteCandidate> fetchAllCandidates(RSCGroup::DbusClient& client)
{
    std::vector<RSCGroup::RemoteCandidate> candidates;
    for (const auto& mac : client.getRemoteCandidateMacs()) {
        if (auto c = client.getCandidateByMac(mac))
            candidates.push_back(std::move(*c));
    }
    return candidates;
}

/**
 * @brief Build a JSON array of LLDP neighbor details for a given interface.
 */
Json::Value getLldpNeighbors(
    const std::string& ifname,
    const std::vector<RSCGroup::RemoteCandidate>& candidates)
{
    Json::Value arr;
    for (const auto& c : candidates) {
        if (!c.seenInLldp) continue;
        if (!c.neighborIfaces.contains(ifname)) continue;

        Json::Value entry;
        if (c.remoteChassisId)  entry["chassisId"]  = *c.remoteChassisId;
        if (c.remotePortId)     entry["portId"]     = *c.remotePortId;
        if (c.remoteSystemName) entry["systemName"] = *c.remoteSystemName;
        arr.append(entry);
    }
    return arr;
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

void printStatus(RSCGroup::DbusClient& client)
{
    if (!FLAGS_interface.empty()) {
        auto iface = client.getInterface(FLAGS_interface);
        if (iface) {
            Json::Value root;
            root[FLAGS_interface] = ifaceToJson(*iface);

            if (FLAGS_lldp) {
                auto candidates = fetchAllCandidates(client);
                root[FLAGS_interface]["lldpNeighbors"] =
                    getLldpNeighbors(FLAGS_interface, candidates);
            }

            printJson(root);
        } else {
            std::cout << "{}\n";
        }
    } else {
        auto snap = client.getLocalSnapshot();
        std::vector<RSCGroup::RemoteCandidate> candidates;
        if (FLAGS_lldp) {
            candidates = fetchAllCandidates(client);
        }

        Json::Value root;
        for (const auto& [name, iface] : snap.interfaces) {
            root[name] = ifaceToJson(iface);
            if (FLAGS_lldp) {
                root[name]["lldpNeighbors"] =
                    getLldpNeighbors(name, candidates);
            }
        }
        printJson(root);
    }
}

void printRemote(RSCGroup::DbusClient& client)
{
    if (!FLAGS_mac.empty()) {
        auto c = client.getCandidateByMac(FLAGS_mac);
        if (c) {
            if (FLAGS_lldp && !c->seenInLldp) {
                std::cout << "{}\n";
                return;
            }
            printJson(candidateToJson(*c));
        } else {
            std::cout << "{}\n";
        }
    } else {
        auto candidates = fetchAllCandidates(client);
        Json::Value arr;
        for (const auto& c : candidates) {
            if (FLAGS_lldp && !c.seenInLldp) continue;
            arr.append(candidateToJson(c));
        }
        printJson(arr);
    }
}

void watchEvents(RSCGroup::DbusClient& client)
{
    client.onLocalStateChanged([]() {
        std::cout << "[event] LocalStateChanged\n";
    });

    client.onInterfaceChanged([](const std::string& ifname) {
        std::cout << "[event] InterfaceChanged: " << ifname << "\n";
    });

    client.onInterfaceRemoved([](const std::string& ifname) {
        std::cout << "[event] InterfaceRemoved: " << ifname << "\n";
    });

    client.onCandidateChanged([](const std::string& mac) {
        std::cout << "[event] CandidateChanged: " << mac << "\n";
    });

    client.onCandidateRemoved([](const std::string& mac) {
        std::cout << "[event] CandidateRemoved: " << mac << "\n";
    });

    client.onReadyChanged([](bool ready) {
        std::cout << "[event] ReadyChanged: " << (ready ? "ready" : "not ready") << "\n";
    });

    LOG(INFO) << "Watching events... (Ctrl+C to stop)";
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(120));
    }
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    google::InitGoogleLogging(argv[0]);
    gflags::SetUsageMessage("Network Observation CLI");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    RSCGroup::DbusClient client(FLAGS_bus);
    if (!client.connect()) {
        std::cerr << "Failed to connect to network-observationd\n";
        return 1;
    }

    if (FLAGS_command == "status") {
        printStatus(client);
    } else if (FLAGS_command == "remote") {
        printRemote(client);
    } else if (FLAGS_command == "watch") {
        watchEvents(client);
    } else if (FLAGS_command == "monitor") {
        printStatus(client);
        printRemote(client);
        watchEvents(client);
    } else {
        std::cerr << "Unknown command: " << FLAGS_command << "\n";
        std::cerr << "Available: status, remote, watch, monitor\n";
        return 1;
    }

    return 0;
}