#pragma once

#pragma once

#include "IObservationRuntime.h"

#include <memory>

namespace RSCGroup {

class INetworkObservationModel;
class LldpObserver;
class ModelConfig;
class NetlinkNetworkMonitor;

/**
 * Production observation runtime backed by netlink and LLDP inputs.
 *
 * The runtime coordinates input producers and delegates model behavior through
 * INetworkObservationModel. It does not expose engine, classifier, or policy
 * implementations.
 */
class NetlinkLldpObservationRuntime final : public IObservationRuntime {
public:
    explicit NetlinkLldpObservationRuntime(ModelConfig config);
    explicit NetlinkLldpObservationRuntime(std::unique_ptr<INetworkObservationModel> model);
    ~NetlinkLldpObservationRuntime() override;
    NetlinkLldpObservationRuntime(const NetlinkLldpObservationRuntime&) = delete;
    NetlinkLldpObservationRuntime& operator=(const NetlinkLldpObservationRuntime&) = delete;

    [[nodiscard]] bool start() override;
    void stop() override;
    [[nodiscard]] bool isRunning() const override;
    [[nodiscard]] ObservationRuntimeHealth health() const override;

    void setInterfacePolicy(std::unique_ptr<IInterfacePolicy> policy) override;
    void setEventSink(IModelEventSink* sink) override;
    void setClassifier(std::unique_ptr<ICandidateClassifier> classifier) override;

    [[nodiscard]] LocalNetworkSnapshot localSnapshot() const override;
    [[nodiscard]] std::vector<RemoteCandidate> remoteCandidates() const override;
    [[nodiscard]] std::optional<RemoteCandidate> findCandidateByMac(const std::string& mac) const override;

    void age(std::chrono::steady_clock::time_point now) override;

private:
    std::unique_ptr<INetworkObservationModel> model_;
    std::unique_ptr<NetlinkNetworkMonitor> monitor_;
    std::unique_ptr<LldpObserver> lldpObserver_;
};

} // namespace RSCGroup