#pragma once
#include <ServiceBase.h>

#include "NetworkObservationAdapter.h"
#include "ITransport.h"
#include "IObservationQueryService.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace RSCGroup {

class ObservationService : public ServiceBase, public IModelEventSink, public IObservationQueryService {
public:
    ObservationService(std::unique_ptr<NetworkObservationAdapter> adapter, std::unique_ptr<ITransport> transport);
    ~ObservationService() = default;

    // ServiceBase extension points
    bool initializeComponents() override;
    void validateConfiguration() override;

    [[nodiscard]] bool start() override;
    void stop() override;

    void onModelEvent(const ModelEvent& event) override;

    // --- IObservationQueryService ---
    LocalNetworkSnapshot localSnapshot() const override;
    std::optional<LocalInterfaceState> getInterface(const std::string& ifname) const override;
    std::vector<RemoteCandidate> remoteCandidates() const override;
    std::optional<RemoteCandidate> getCandidateByMac(const std::string& mac) const override;
    bool isReady() const override { return ServiceBase::isReady(); }

private:
    void agingLoop(std::stop_token st);

    static constexpr std::chrono::seconds kAgingInterval{10};

    std::unique_ptr<NetworkObservationAdapter> adapter_;
    std::unique_ptr<ITransport> transport_;
    // Declared last: the aging thread must stop before adapter_ is destroyed.
    std::mutex agingMutex_;
    std::condition_variable_any agingCv_;
    std::jthread agingThread_;
};

} // namespace RSCGroup
