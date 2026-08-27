//
// Created by vvass on 21-Jul-26.
//
/**
 * @file DbusClient.h
 * @brief Client-side typed D-Bus proxy for network-observationd.
 */
#pragma once
#include "LocalStateTypes.h"
#include "CandidateTypes.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace RSCGroup {

class DbusClient {
public:
    using StringCallback = std::function<void(const std::string&)>;
    using BoolCallback   = std::function<void(bool)>;
    using VoidCallback   = std::function<void()>;

    explicit DbusClient(const std::string& busType = "system");
    ~DbusClient();

    DbusClient(const DbusClient&) = delete;
    DbusClient& operator=(const DbusClient&) = delete;

    [[nodiscard]] bool connect();

    // --- Typed query methods ---
    LocalNetworkSnapshot getLocalSnapshot();
    std::optional<LocalInterfaceState> getInterface(const std::string& ifname);
    std::vector<std::string> getRemoteCandidateMacs();
    std::optional<RemoteCandidate> getCandidateByMac(const std::string& mac);
    bool getReady();
    std::string getPhase();

    // --- Signal subscriptions ---
    void onLocalStateChanged(VoidCallback cb);
    void onInterfaceChanged(StringCallback cb);
    void onCandidateChanged(StringCallback cb);
    void onInterfaceRemoved(StringCallback cb);
    void onCandidateRemoved(StringCallback cb);
    void onReadyChanged(BoolCallback cb);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace RSCGroup