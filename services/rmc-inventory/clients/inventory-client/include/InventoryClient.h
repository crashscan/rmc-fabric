#pragma once

#include <ClientResult.hpp>
#include <inventory.hpp>

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace RSCGroup {

class InventoryClient {
public:
    using StringCallback = std::function<void(const std::string&)>;
    using BoolCallback   = std::function<void(bool)>;

    explicit InventoryClient(
        std::shared_ptr<void> connection,
        std::string serviceName    = std::string(interop_contract::inventory::SERVICE_NAME),
        std::string objectPath     = std::string(interop_contract::inventory::OBJECT_PATH),
        std::string interfaceName  = std::string(interop_contract::inventory::INTERFACE)
    );
    explicit InventoryClient(
        std::string busType,
        std::string serviceName    = std::string(interop_contract::inventory::SERVICE_NAME),
        std::string objectPath     = std::string(interop_contract::inventory::OBJECT_PATH),
        std::string interfaceName  = std::string(interop_contract::inventory::INTERFACE)
    );
    ~InventoryClient();

    InventoryClient(const InventoryClient&) = delete;
    InventoryClient& operator=(const InventoryClient&) = delete;

    [[nodiscard]] interop_contract::ClientResult<interop_contract::inventory::InventorySnapshot> tryGetIdentity() const;
    [[nodiscard]] interop_contract::ClientResult<interop_contract::inventory::InventoryFields> tryGetField(const std::string& fieldName) const;
    [[nodiscard]] interop_contract::ClientResult<interop_contract::inventory::SourceStateMap> tryGetSourceStates() const;
    [[nodiscard]] interop_contract::ClientResult<bool> tryGetReady() const;
    [[nodiscard]] interop_contract::ClientResult<std::string> tryGetPhase() const;
    [[nodiscard]] interop_contract::ClientResult<uint64_t> tryGetVersion() const;
    [[nodiscard]] interop_contract::ClientResult<interop_contract::inventory::InventoryIssues> tryGetIssues() const;
    [[nodiscard]] interop_contract::ClientResult<void> tryRefresh() const;
    [[nodiscard]] interop_contract::ClientResult<bool> tryWaitReady(std::chrono::milliseconds timeout) const;

    [[nodiscard]] interop_contract::inventory::InventorySnapshot getIdentity() const;
    [[nodiscard]] interop_contract::inventory::InventoryFields getField(const std::string& fieldName) const;
    [[nodiscard]] interop_contract::inventory::SourceStateMap getSourceStates() const;
    [[nodiscard]] bool getReady() const;
    [[nodiscard]] std::string getPhase() const;
    [[nodiscard]] uint64_t getVersion() const;
    [[nodiscard]] interop_contract::inventory::InventoryIssues getIssues() const;

    void refresh() const;

    // Signal subscriptions. Caller is responsible for running the DBus dispatcher/event loop.
    // Repeated calls append listeners.
    void onInventoryChanged(StringCallback cb);
    void onSourceStateChanged(StringCallback cb);
    void onReadyChanged(BoolCallback cb);

    // Polls GetReady() until true or timeout expires.
    [[nodiscard]] bool waitReady(std::chrono::milliseconds timeout) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace RSCGroup
