//
// Created by vvass on 20-Jul-26.
//
/**
 * @file INetworkObservationModel.h
 * @brief Public API for the network observation model.
 */
#pragma once
#include "CandidateTypes.h"
#include "LocalStateTypes.h"
#include "ObservationTypes.h"
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace RSCGroup {

class IInterfacePolicy;
class ICandidateClassifier;

enum class ModelEventKind {
    LocalInterfaceChanged,
    LocalInterfaceRemoved,
    LocalAddressChanged,
    CandidateAdded,
    CandidateUpdated,
    CandidateConfirmed,
    CandidateAged,
    CandidateExpired,
    CandidateRemoved,
    ClassificationChanged
};

struct ModelEvent {
    ModelEventKind kind;
    std::chrono::steady_clock::time_point timestamp;
    std::optional<std::string> ifname;
    std::optional<std::string> mac;
};

class IModelEventSink {
public:
    virtual ~IModelEventSink() = default;
    virtual void onModelEvent(const ModelEvent&) = 0;
};

class INetworkObservationModel {
public:
    virtual ~INetworkObservationModel() = default;

    virtual void setEventSink(IModelEventSink* sink) = 0;

    virtual void onLinkObservation(const LinkObservation& obs) = 0;
    virtual void onAddressObservation(const AddressObservation& obs) = 0;
    virtual void onNeighborObservation(const NeighborObservation& obs) = 0;
    virtual void onFdbObservation(const FdbObservation& obs) = 0;
    virtual void onLldpObservation(const LldpObservation& obs) = 0;

    virtual LocalNetworkSnapshot localSnapshot() const = 0;
    virtual std::vector<RemoteCandidate> remoteCandidates() const = 0;
    virtual std::optional<RemoteCandidate> findCandidateByMac(const std::string& mac) const = 0;

    virtual void age(std::chrono::steady_clock::time_point now) = 0;

    /// Prepare the model for a (re)start of observation sources.
    /// Re-enters Initializing, demotes all candidates to Provisional,
    /// clears per-source evidence and local state, preserving identity
    /// and timestamps (firstSeen/lastSeen). Emits CandidateRemoved for
    /// previously-publishable candidates. The next initial dump rebuilds
    /// evidence; markLive() promotes only re-observed candidates.
    virtual void prepareForRestart() = 0;

    /// Exit Initializing: promote worthy provisional candidates to
    /// Confirmed (emitting CandidateUpdated), erase the rest.
    virtual void markLive() = 0;

    virtual void setInterfacePolicy(std::unique_ptr<IInterfacePolicy> policy) = 0;
    virtual void setClassifier(std::unique_ptr<ICandidateClassifier> classifier) = 0;
};

} // namespace RSCGroup