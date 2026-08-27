#pragma once

#include <inventory_service_api/ITransport.h>

#include <memory>
#include <string>
#include <atomic>

namespace DBus {
class Connection;
class Object;
}

namespace RSCGroup {

class DbusServiceAdapter;

class DbusTransportBase : public ITransport {
public:
    DbusTransportBase(std::shared_ptr<DBus::Connection> connection,
                      std::unique_ptr<DbusServiceAdapter> adapter,
                      std::string serviceName,
                      std::string objectPath,
                      std::string interfaceName);

    ~DbusTransportBase() override;

    DbusTransportBase(const DbusTransportBase&) = delete;
    DbusTransportBase& operator=(const DbusTransportBase&) = delete;

    void start(IInventoryQueryService& queryService) override;
    void stop() override;

    void publishInventoryChanged(const std::string& fieldPath) override;
    void publishSourceStateChanged(const std::string& sourceName) override;
    void publishReadyChanged(bool ready) override;

    [[nodiscard]] bool isRunning() const { return running_.load(std::memory_order_acquire); }

protected:
    [[nodiscard]] std::shared_ptr<DBus::Connection> getConnection() const { return connection_; }
    [[nodiscard]] std::shared_ptr<DBus::Object> getObject() const { return object_; }
    [[nodiscard]] DbusServiceAdapter* getAdapter() const { return adapter_.get(); }

    virtual void onAdapterBound() {}

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::shared_ptr<DBus::Connection> connection_;
    std::shared_ptr<DBus::Object> object_;
    std::unique_ptr<DbusServiceAdapter> adapter_;

    std::string serviceName_;
    std::string objectPath_;
    std::string interfaceName_;

    std::atomic<bool> running_{false};
};

} // namespace RSCGroup
