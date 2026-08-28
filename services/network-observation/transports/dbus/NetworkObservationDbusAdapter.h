#pragma once

#include <DbusServiceAdapter.h>

#include <memory>
#include <string>

namespace DBus {
class Object;
template<typename> class Signal;
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
 */
class NetworkObservationDbusAdapter : public DbusServiceAdapter {
public:
    NetworkObservationDbusAdapter();
    ~NetworkObservationDbusAdapter() override = default;

    NetworkObservationDbusAdapter(const NetworkObservationDbusAdapter&) = delete;
    NetworkObservationDbusAdapter& operator=(const NetworkObservationDbusAdapter&) = delete;

    void setService(IObservationQueryService* service);
    [[nodiscard]] IObservationQueryService* getService() const;

    void bind(const std::shared_ptr<DBus::Object>& object,
              const std::string& interfaceName) override;

    void onTransportStopping() override;

    void publishLocalStateChanged();
    void publishInterfaceChanged(const std::string& ifname);
    void publishInterfaceRemoved(const std::string& ifname);
    void publishCandidateChanged(const std::string& mac);
    void publishCandidateRemoved(const std::string& mac);
    void publishReadyChanged(bool ready);

private:
    IObservationQueryService* service_ = nullptr;
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
