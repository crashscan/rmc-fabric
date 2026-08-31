#pragma once

#include <ServiceBase.h>

#include "IObservationQueryService.h"
#include "IObservationRuntime.h"
#include "IObservationTransport.h"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace RSCGroup {

class ObservationService final : public ServiceBase, public IModelEventSink, public IObservationQueryService {
public:
    ObservationService(std::unique_ptr<IObservationRuntime> runtime,
                       std::shared_ptr<IObservationTransport> transport,
                       std::chrono::steady_clock::duration agingInterval = std::chrono::seconds{10});
    ~ObservationService() override;

    [[nodiscard]] bool initializeComponents() override;
    void validateConfiguration() override;

    [[nodiscard]] bool start() override;
    void stop() override;

    void onModelEvent(const ModelEvent& event) override;

    [[nodiscard]] LocalNetworkSnapshot localSnapshot() const override;
    [[nodiscard]] std::optional<LocalInterfaceState> getInterface(const std::string& ifname) const override;
    [[nodiscard]] std::vector<RemoteCandidate> remoteCandidates() const override;
    [[nodiscard]] std::optional<RemoteCandidate> getCandidateByMac(const std::string& mac) const override;
    [[nodiscard]] bool isReady() const override { return ServiceBase::isReady(); }

private:
    void agingLoop(std::stop_token st);
    void stopOwnedState();

    std::unique_ptr<IObservationRuntime> runtime_;
    std::chrono::steady_clock::duration agingInterval_;
    mutable std::mutex lifecycleMutex_;
    std::mutex agingMutex_;
    std::condition_variable_any agingCv_;
    std::jthread agingThread_;
};

} // namespace RSCGroup
