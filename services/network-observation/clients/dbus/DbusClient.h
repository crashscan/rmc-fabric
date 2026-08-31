//
// Created by vvass on 21-Jul-26.
//
/**
 * @file DbusClient.h
 * @brief Client-side typed D-Bus proxy for network-observationd.
 */
#pragma once
#include <ClientResult.hpp>
#include <network_observation/NetworkObservationTypes.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace RSCGroup {

namespace contract = interop_contract::network_observation;

class DbusClient {
public:
    using StringCallback = std::function<void(const std::string&)>;
    using BoolCallback   = std::function<void(bool)>;
    using VoidCallback   = std::function<void()>;

    explicit DbusClient(const std::string& busType = "system");
    ~DbusClient();

    DbusClient(const DbusClient&) = delete;
    DbusClient& operator=(const DbusClient&) = delete;

    [[nodiscard]] interop_contract::ClientResult<void> tryConnect();
    [[nodiscard]] bool connect();

    // --- Typed query methods ---
    [[nodiscard]] interop_contract::ClientResult<contract::LocalNetworkSnapshot> tryGetLocalSnapshot();
    [[nodiscard]] interop_contract::ClientResult<std::optional<contract::LocalInterfaceState>> tryGetInterface(const std::string& ifname);
    [[nodiscard]] interop_contract::ClientResult<std::vector<std::string>> tryGetRemoteCandidateMacs();
    [[nodiscard]] interop_contract::ClientResult<std::optional<contract::RemoteCandidate>> tryGetCandidateByMac(const std::string& mac);
    [[nodiscard]] interop_contract::ClientResult<contract::ObservationIssues> tryGetIssues();
    [[nodiscard]] interop_contract::ClientResult<bool> tryGetReady();
    [[nodiscard]] interop_contract::ClientResult<std::string> tryGetPhase();

    contract::LocalNetworkSnapshot getLocalSnapshot();
    std::optional<contract::LocalInterfaceState> getInterface(const std::string& ifname);
    std::vector<std::string> getRemoteCandidateMacs();
    std::optional<contract::RemoteCandidate> getCandidateByMac(const std::string& mac);
    contract::ObservationIssues getIssues();
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