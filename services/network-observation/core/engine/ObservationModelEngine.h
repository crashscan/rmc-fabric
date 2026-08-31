//
// Created by vvass on 20-Jul-26.
//
#pragma once
#include "INetworkObservationModel.h"
#include "ModelConfig.h"
#include "IInterfacePolicy.h"
#include "ICandidateClassifier.h"
#include "HardFilter.h"
#include "LocalStateTracker.h"
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace RSCGroup {

class ObservationModelEngine : public INetworkObservationModel {
public:
    explicit ObservationModelEngine(ModelConfig config);

    void setEventSink(IModelEventSink* sink) override;

    void onLinkObservation(const LinkObservation& obs) override;
    void onAddressObservation(const AddressObservation& obs) override;
    void onNeighborObservation(const NeighborObservation& obs) override;
    void onFdbObservation(const FdbObservation& obs) override;
    void onLldpObservation(const LldpObservation& obs) override;

    LocalNetworkSnapshot localSnapshot() const override;
    std::vector<RemoteCandidate> remoteCandidates() const override;
    std::optional<RemoteCandidate> findCandidateByMac(const std::string& mac) const override;

    void age(std::chrono::steady_clock::time_point now) override;

    /// Re-enter Initializing for a source (re)start; see interface docs.
    void prepareForRestart() override;

    /// Promote worthy provisional candidates, erase the rest.
    void markLive() override;

    /// Runtime config mutations
    void setInterfacePolicy(std::unique_ptr<IInterfacePolicy> policy);
    void setClassifier(std::unique_ptr<ICandidateClassifier> classifier);

private:
    enum class ModelPhase { Initializing, Live };

    ModelConfig config_;
    IModelEventSink* sink_ = nullptr;
    ModelPhase phase_ = ModelPhase::Initializing;
    mutable std::mutex mutex_;

    std::unique_ptr<IInterfacePolicy> interfacePolicy_;
    std::unique_ptr<ICandidateClassifier> classifier_;
    HardFilter hardFilter_;
    LocalStateTracker localState_;
    std::unordered_map<std::string, RemoteCandidate> candidates_;

    void reconcileAffectedByLocalMac(std::string_view mac);
    void reconcileAffectedByLocalIp(const std::string& ip);
    void reconcileByKey(std::string_view mac);
    RemoteCandidate& getOrCreateCandidate(const std::string& mac);
    static bool isPublishable(const RemoteCandidate& c);
    void updateClassification(RemoteCandidate& c);

    /// Revival + phase-based status transitions.
    /// Precondition: mutex_ held, updateClassification() already applied.
    void reconcileStatusLocked(RemoteCandidate& c, ObservationEvent event);
};

} // namespace RSCGroup
