#pragma once

#include "CandidateTypes.h"
#include "INetworkObservationModel.h"
#include "IObservationRuntime.h"
#include "ModelConfig.h"
#include "LocalStateTypes.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace RSCGroup {

class NetlinkNetworkMonitor;
class LldpObserver;

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
