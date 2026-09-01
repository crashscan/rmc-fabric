#pragma once

#include <DbusServiceAdapter.h>
#include <ServiceBinding.h>

#include <memory>
#include <string>
#include <map>

namespace DBus {
class Object;
template<typename...> class Signal;
}

namespace RSCGroup {

class IInventoryQueryService;
struct InventoryQueryHandler;

class InventoryDbusAdapter : public DbusServiceAdapter {
public:
    InventoryDbusAdapter();

    InventoryDbusAdapter(const InventoryDbusAdapter&) = delete;
    InventoryDbusAdapter& operator=(const InventoryDbusAdapter&) = delete;

    void setService(IInventoryQueryService* service);

    void bind(const std::shared_ptr<DBus::Object>& object,
              const std::string& interfaceName) override;

    /**
     * @brief Closes query admission and waits for in-flight D-Bus handler
     *        calls to complete.  Safe to call concurrently with handler
     *        method invocations.
     *
     * After this returns, no query handler executes and no new query is
     * admitted.  Publication signals remain open.
     */
    void quiesceQueries() noexcept override;

    /**
     * @brief Revokes handler access to the service and waits for in-flight
     *        D-Bus calls to complete.  Safe to call concurrently with handler
     *        method invocations.
     *
     * Delegates to quiesceQueries() for the drain; retained for compatibility
     * with the DbusServiceAdapter::onTransportStopping() contract.
     */
    void onTransportStopping() override;

    void publishInventoryChanged(const std::string& fieldPath);
    void publishSourceStateChanged(const std::string& sourceName);
    void publishReadyChanged(bool ready);

private:
    /// Synchronized binding: guards concurrent handler access vs shutdown.
    /// @see ServiceBinding for the ownership/lifetime invariant.
    ServiceBinding<IInventoryQueryService> binding_;

    std::shared_ptr<InventoryQueryHandler> handler_;

    std::shared_ptr<DBus::Signal<void(std::string)>> signalInventoryChanged_;
    std::shared_ptr<DBus::Signal<void(std::string)>> signalSourceStateChanged_;
    std::shared_ptr<DBus::Signal<void(bool)>> signalReadyChanged_;

    void createSignals(const std::shared_ptr<DBus::Object>& object,
                       const std::string& interfaceName);
    void bindMethods(const std::shared_ptr<DBus::Object>& object,
                     const std::string& interfaceName);
};

} // namespace RSCGroup
