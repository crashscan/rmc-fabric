//
// Created by vvass on 20-Jul-26.
//
/**
 * @file NetworkObservationAdapter.h
 * @brief Sole public API — bridges netlink monitor + LLDP observer to observation model.
 */
#pragma once
#include "INetworkObservationModel.h"
#include "ModelConfig.h"
#include <memory>
#include <set>
#include <string>

namespace RSCGroup {

class NetlinkNetworkMonitor;
class LldpObserver;

/**
 * @brief Single-entry-point for network observation.
 *
 * Owns and orchestrates netlink and LLDP observation sources, feeding
 * both into a shared observation model. Constructs the model engine,
 * classifier, and interface policy from ModelConfig.
 *
 * The adapter is restartable. stop() destroys the monitor/LLDP sources.
 * Persists across restart: candidate identity and timestamps
 * (firstSeen/lastSeen), classifier and policy. Does NOT persist:
 * per-source evidence, local interface state, publishability.
 * On the next start(), prepareForRestart() demotes all candidates to
 * Provisional and clears evidence; the fresh initial dump rebuilds it;
 * markLive() promotes only re-observed candidates.
 */
class NetworkObservationAdapter {
public:
    /// Construct adapter with default ObservationModelEngine from config
    explicit NetworkObservationAdapter(ModelConfig config);

    /// Construct adapter with a custom INetworkObservationModel implementation
    explicit NetworkObservationAdapter(std::unique_ptr<INetworkObservationModel> model);

    ~NetworkObservationAdapter();

    NetworkObservationAdapter(const NetworkObservationAdapter&) = delete;
    NetworkObservationAdapter& operator=(const NetworkObservationAdapter&) = delete;

    [[nodiscard]] bool start();
    void stop();
    [[nodiscard]] bool isRunning() const;

    /// Replace the interface policy at runtime (thread-safe)
    void setInterfacePolicy(std::unique_ptr<IInterfacePolicy> policy);

    /// Register an event sink for model events
    void setEventSink(IModelEventSink* sink);

    /// Replace the classifier at runtime (thread-safe)
    void setClassifier(std::unique_ptr<ICandidateClassifier> classifier);

    // --- Queries delegated to internal model ---

    LocalNetworkSnapshot localSnapshot() const;
    std::vector<RemoteCandidate> remoteCandidates() const;
    std::optional<RemoteCandidate> findCandidateByMac(const std::string& mac) const;
    void age(std::chrono::steady_clock::time_point now);

private:
    std::unique_ptr<INetworkObservationModel> model_;
    std::unique_ptr<NetlinkNetworkMonitor> monitor_;
    std::unique_ptr<LldpObserver> lldpObserver_;
};

} // namespace RSCGroup
