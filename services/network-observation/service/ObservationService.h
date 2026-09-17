#pragma once

#include <LifecycleCoordinator.h>
#include <ManagedWorker.h>
#include <ServiceBase.h>

#include "IObservationQueryService.h"
#include "IObservationRuntime.h"
#include "IObservationTransport.h"
#include "INetworkObservationModel.h"

#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
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

    void addTransport(std::shared_ptr<IObservationTransport> transport);

    void onModelEvent(const ModelEvent &event) override;

    [[nodiscard]] LocalNetworkSnapshot localSnapshot() const override;

    [[nodiscard]] std::optional<LocalInterfaceState> getInterface(const std::string &ifname) const override;

    [[nodiscard]] std::vector<RemoteCandidate> remoteCandidates() const override;

    [[nodiscard]] std::optional<RemoteCandidate> getCandidateByMac(const std::string &mac) const override;

    [[nodiscard]] interop_contract::network_observation::ObservationIssues getIssues() const override;

    [[nodiscard]] bool isReady() const override { return ServiceBase::isReady(); }

    [[nodiscard]] std::string getPhase() const override;

private:
    void agingLoop(std::stop_token st);

    void supervisionLoop(std::stop_token stopToken);

    void onAgingWorkerExit(const ManagedWorker::Exit &exit);

    void onSupervisionWorkerExit(const ManagedWorker::Exit &exit);

    void refreshRuntimeIssues();

    void reportIssue(const std::string &issueCode,
                     const std::string &severity,
                     const std::string &component,
                     const std::string &operation,
                     const std::string &category,
                     const std::string &identity,
                     const std::string &message);

    void clearIssue(const std::string &issueCode,
                    const std::string &component,
                    const std::string &identity,
                    const std::string &message);

    void noteTransportPublishFailure(const std::string &transportName,
                                     const std::string &operation,
                                     const std::string &message);

    void clearTransportPublishFailure(const std::string &transportName,
                                      const std::string &operation);

    std::unique_ptr<IObservationRuntime> runtime_;
    std::chrono::steady_clock::duration agingInterval_;
    /// tick() cadence — kept at the aging cadence tick() previously ran at.
    std::chrono::steady_clock::duration supervisionInterval_;
    std::mutex agingMutex_;
    std::condition_variable_any agingCv_;
    /// Supervision has its own pair: sharing agingCv_ makes every
    /// notify_all() cross-wake the other loop, and agingLoop's
    /// end-of-body re-lock would contend with a supervision cycle —
    /// reintroducing the coupling the worker split exists to remove.
    std::mutex supervisionMutex_;
    std::condition_variable_any supervisionCv_;
    mutable std::mutex issuesMutex_;
    interop_contract::network_observation::ObservationIssues issues_;

    /// Serializes complete service-epoch transitions only.
    LifecycleCoordinator lifecycle_;

    // ------------------------------------------------------------------ //
    // Member destruction order
    // ------------------------------------------------------------------ //
    // The workers MUST be the last members: their work/wake/exit callbacks
    // capture `this` and access runtime_, the intervals, agingMutex_,
    // agingCv_, issuesMutex_, and issues_.  Declaring them last means
    // reverse member destruction stops and joins them before any state
    // they touch goes away.
    ManagedWorker supervisionWorker_;
    ManagedWorker agingWorker_;
};
} // namespace RSCGroup