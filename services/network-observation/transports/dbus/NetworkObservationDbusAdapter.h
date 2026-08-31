#pragma once

#include <DbusServiceAdapter.h>
#include <ServiceBinding.h>

#include <memory>
#include <string>

namespace DBus {
class Object;
template<typename...> class Signal;
}

namespace RSCGroup {

class IObservationQueryService;
struct NetworkObservationHandler;

/**
 * @brief D-Bus adapter for the network-observation service.
 *
 * Implements DbusServiceAdapter for the network-observation domain:
 * registers D-Bus methods (GetLocalSnapshot, GetInterface, etc.) and
 * creates typed signals (LocalStateChanged, InterfaceChanged, etc.).
 *
 * Ownership/lifetime invariant
 * ----------------------------
 * The query service is externally owned.  setService() registers the pointer
 * via ServiceBinding.  onTransportStopping() calls binding_.detach(), which
 * blocks until all in-flight D-Bus handler calls complete, then revokes
 * access.  Any call arriving after detach() returns the safe default.
 */
class NetworkObservationDbusAdapter : public DbusServiceAdapter {
public:
    NetworkObservationDbusAdapter();
    ~NetworkObservationDbusAdapter() override = default;

    NetworkObservationDbusAdapter(const NetworkObservationDbusAdapter&) = delete;
    NetworkObservationDbusAdapter& operator=(const NetworkObservationDbusAdapter&) = delete;

    void setService(IObservationQueryService* service);

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
    void quiesceQueries() override;

    /**
     * @brief Revokes handler access to the service and waits for in-flight
     *        D-Bus calls to complete.  Safe to call concurrently with handler
     *        method invocations.
     *
     * Delegates to quiesceQueries() for the drain.
     */
    void onTransportStopping() override;

    void publishLocalStateChanged();
    void publishInterfaceChanged(const std::string& ifname);
    void publishInterfaceRemoved(const std::string& ifname);
    void publishCandidateChanged(const std::string& mac);
    void publishCandidateRemoved(const std::string& mac);
    void publishReadyChanged(bool ready);

private:
    /// Synchronized binding: guards concurrent handler access vs shutdown.
    /// @see ServiceBinding for the ownership/lifetime invariant.
    ServiceBinding<IObservationQueryService> binding_;

    std::shared_ptr<NetworkObservationHandler> handler_;

    std::shared_ptr<DBus::Signal<void()>>           signalLocalStateChanged_;
    std::shared_ptr<DBus::Signal<void(std::string)>> signalInterfaceChanged_;
    std::shared_ptr<DBus::Signal<void(std::string)>> signalInterfaceRemoved_;
    std::shared_ptr<DBus::Signal<void(std::string)>> signalCandidateChanged_;
    std::shared_ptr<DBus::Signal<void(std::string)>> signalCandidateRemoved_;
    std::shared_ptr<DBus::Signal<void(bool)>>        signalReadyChanged_;

    void createSignals(const std::shared_ptr<DBus::Object>& object,
                       const std::string& interfaceName);
    void bindMethods(const std::shared_ptr<DBus::Object>& object,
                     const std::string& interfaceName);
};

} // namespace RSCGroup
