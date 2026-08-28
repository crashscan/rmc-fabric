#pragma once

#include <memory>
#include <string>
#include <atomic>

namespace DBus {
class Connection;
class Object;
class StandaloneDispatcher;
}

namespace RSCGroup {

class DbusServiceAdapter;

/**
 * @brief Generic D-Bus service infrastructure: connection management, object
 *        registration, and adapter binding.
 *
 * This class is service-agnostic. Concrete transports inherit from both their
 * service-specific ITransport and from DbusTransportBase, and delegate D-Bus
 * lifecycle to this base.
 *
 * Two construction modes are supported:
 *  - External connection: caller creates and owns the dispatcher/connection.
 *  - Internal connection: DbusTransportBase creates its own dispatcher and
 *    connection lazily when start() is called, identified by busType string.
 */
class DbusTransportBase {
public:
    /// External-connection constructor (e.g. rmc-inventory where the caller
    /// creates the dispatcher/connection).
    DbusTransportBase(std::shared_ptr<DBus::Connection> connection,
                      std::unique_ptr<DbusServiceAdapter> adapter,
                      std::string serviceName,
                      std::string objectPath,
                      std::string interfaceName);

    /// Internal-connection constructor: DbusTransportBase creates its own
    /// StandaloneDispatcher and connection in start().  @p busType is
    /// "system" or "session".
    DbusTransportBase(std::string busType,
                      std::unique_ptr<DbusServiceAdapter> adapter,
                      std::string serviceName,
                      std::string objectPath,
                      std::string interfaceName);

    virtual ~DbusTransportBase();

    DbusTransportBase(const DbusTransportBase&) = delete;
    DbusTransportBase& operator=(const DbusTransportBase&) = delete;

    /**
     * @brief Register the bus name, create the D-Bus object, and call
     *        adapter->bind().  Throws on failure.
     *
     * The concrete transport must set the query service on the adapter
     * before calling start().
     */
    void start();
    void stop();

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

    std::string busType_;
    std::string serviceName_;
    std::string objectPath_;
    std::string interfaceName_;

    std::atomic<bool> running_{false};
};

} // namespace RSCGroup
