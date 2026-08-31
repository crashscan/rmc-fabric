//
// Created by vvass on 20-Jul-26.
//
#include "ObservationModelEngine.h"
#include "ClassifierFactory.h"
#include "IInterfacePolicy.h"
#include "LldpUtils.h"
#include <chrono>
#include <set>
#include <sys/socket.h>

namespace RSCGroup {

namespace {

std::string extractIpFromCidr(const std::string& cidr)
{
    auto pos = cidr.find('/');
    if (pos == std::string::npos) return {};
    return cidr.substr(0, pos);
}

/// Candidate events collected under lock, delivered after unlock.
/// Sets dedupe and give deterministic (sorted) emission order.
struct PendingEvents {
    std::set<std::string> changed;   // publishable and may have changed
    std::set<std::string> removed;   // was publishable, now gone or hidden

    void record(const std::string& mac, bool wasPublishable, bool nowPublishable) {
        if (nowPublishable)      changed.insert(mac);
        else if (wasPublishable) removed.insert(mac);
    }
    void recordErased(const std::string& mac, bool wasPublishable) {
        if (wasPublishable) removed.insert(mac);
    }
};

void deliverPending(IModelEventSink* sink, const PendingEvents& p)
{
    if (!sink) return;
    const auto now = std::chrono::steady_clock::now();
    for (const auto& mac : p.changed)
        sink->onModelEvent({ModelEventKind::CandidateUpdated, now, std::nullopt, mac});
    for (const auto& mac : p.removed)
        sink->onModelEvent({ModelEventKind::CandidateRemoved, now, std::nullopt, mac});
}

/// Classifications that justify Provisional -> Confirmed promotion.
bool isConfirmedWorthy(CandidateClassification cls)
{
    using enum CandidateClassification;
    switch (cls) {
        case RemoteEndpoint:
        case GatewayLike:
        case TopologyPeer:  return true;
        default:            return false;
    }
}

/// True when no source evidence remains (bridgePort counts as FDB residue).
bool hasNoEvidence(const RemoteCandidate& c)
{
    return !c.seenInNeigh && !c.seenInFdb && !c.seenInLldp &&
           c.ipv4.empty() && c.ipv6.empty() && !c.bridgePort;
}

} // namespace

ObservationModelEngine::ObservationModelEngine(ModelConfig config)
    : config_(std::move(config))
    , hardFilter_(config_)
{
    classifier_ = createClassifier(config_.classifierConfig);
    interfacePolicy_ = config_.interfacePolicy
        ? std::move(config_.interfacePolicy)
        : std::make_unique<DefaultInterfacePolicy>();
}

void ObservationModelEngine::setEventSink(IModelEventSink* sink) {
    std::scoped_lock lock(mutex_);
    sink_ = sink;
}

// ---------------------------------------------------------------------------
// Local observations
// ---------------------------------------------------------------------------

void ObservationModelEngine::onLinkObservation(const LinkObservation& obs) {
    IModelEventSink* sink = nullptr;
    ModelEvent event;
    bool macChanged = false;
    PendingEvents pending;

    {
        std::scoped_lock lock(mutex_);
        if (!interfacePolicy_->includeInLocalState(obs.ifname)) return;

        macChanged = localState_.onLinkObservation(obs);
        event.kind = (obs.event == ObservationEvent::Removed)
            ? ModelEventKind::LocalInterfaceRemoved
            : ModelEventKind::LocalInterfaceChanged;
        event.timestamp = obs.observedAt;
        event.ifname = obs.ifname;
        sink = sink_;
        if (macChanged) {
            // Check publishability before reconciliation
            auto it = candidates_.find(std::string(obs.mac));
            const bool wasPublishable = it != candidates_.end() && isPublishable(it->second);
            reconcileAffectedByLocalMac(obs.mac);
            // After reconciliation the candidate is LocalSelf/Removed — emit if it was visible
            if (wasPublishable)
                pending.removed.insert(std::string(obs.mac));
        }
    }

    if (sink) {
        sink->onModelEvent(event);
        deliverPending(sink, pending);
    }
}

void ObservationModelEngine::onAddressObservation(const AddressObservation& obs) {
    IModelEventSink* sink = nullptr;
    ModelEvent event;
    std::string ip;
    PendingEvents pending;

    {
        std::scoped_lock lock(mutex_);
        if (!interfacePolicy_->includeInLocalState(obs.ifname)) return;

        localState_.onAddressObservation(obs);
        ip = extractIpFromCidr(obs.cidr);
        if (ip.empty()) return;
        event.kind = ModelEventKind::LocalAddressChanged;
        event.timestamp = obs.observedAt;
        event.ifname = obs.ifname;
        sink = sink_;
        // Only reconcile when an address BECOMES local — on removal the IP
        // may legitimately belong to a remote host again.
        if (obs.event == ObservationEvent::Present) {
            // Check publishability before reconciliation for each affected candidate
            for (auto& [mac, c] : candidates_) {
                if (c.ipv4.contains(ip) || c.ipv6.contains(ip)) {
                    if (isPublishable(c))
                        pending.removed.insert(mac);
                }
            }
            reconcileAffectedByLocalIp(ip);
        }
    }

    if (sink) {
        sink->onModelEvent(event);
        deliverPending(sink, pending);
    }
}

// ---------------------------------------------------------------------------
// Remote-candidate observations
// ---------------------------------------------------------------------------

void ObservationModelEngine::onNeighborObservation(const NeighborObservation& obs) {
    IModelEventSink* sink = nullptr;
    PendingEvents pending;
    {
        std::scoped_lock lock(mutex_);
        if (!interfacePolicy_->allowRemoteNeighborEvidence(obs.ifname)) return;

        const bool isLocalMac = localState_.isLocalMac(obs.mac);
        const bool isLocalIp  = localState_.isLocalIp(obs.ip);
        if (!hardFilter_.passes(obs, isLocalMac, isLocalIp)) return;

        auto& c = getOrCreateCandidate(obs.mac);
        const bool wasPublishable = isPublishable(c);

        const NeighborEvidenceKey nkey{obs.ifname, obs.family, obs.ip};
        if (obs.event == ObservationEvent::Present) {
            c.neighborEvidence.insert(nkey);
            c.neighborIfaces.insert(obs.ifname);
            c.lastSeen = obs.observedAt;
            if (obs.family == AF_INET) c.ipv4.insert(obs.ip);
            else                       c.ipv6.insert(obs.ip);
        } else {
            c.neighborEvidence.erase(nkey);
            c.neighborIfaces.erase(obs.ifname);
            if (obs.family == AF_INET) c.ipv4.erase(obs.ip);
            else                       c.ipv6.erase(obs.ip);
        }
        c.seenInNeigh = !c.neighborEvidence.empty();

        if (hasNoEvidence(c)) {
            candidates_.erase(obs.mac);
            pending.recordErased(obs.mac, wasPublishable);
        } else {
            updateClassification(c);
            reconcileStatusLocked(c, obs.event);
            pending.record(obs.mac, wasPublishable, isPublishable(c));
        }
        sink = sink_;
    }
    deliverPending(sink, pending);
}

void ObservationModelEngine::onFdbObservation(const FdbObservation& obs) {
    IModelEventSink* sink = nullptr;
    PendingEvents pending;
    {
        std::scoped_lock lock(mutex_);
        if (!interfacePolicy_->allowRemoteFdbEvidence(obs.portIfname)) return;

        const bool isLocalMac = localState_.isLocalMac(obs.mac);
        if (!hardFilter_.passes(obs, isLocalMac)) return;

        auto& c = getOrCreateCandidate(obs.mac);
        const bool wasPublishable = isPublishable(c);

        const FdbEvidenceKey fkey{obs.portIfname, obs.mac};
        if (obs.event == ObservationEvent::Present) {
            c.fdbEvidence.insert(fkey);
            c.bridgePort = obs.portIfname;
            c.lastSeen = obs.observedAt;
        } else {
            c.fdbEvidence.erase(fkey);
            if (c.bridgePort == obs.portIfname) c.bridgePort.reset();
        }
        c.seenInFdb = !c.fdbEvidence.empty();

        if (hasNoEvidence(c)) {
            candidates_.erase(obs.mac);
            pending.recordErased(obs.mac, wasPublishable);
        } else {
            updateClassification(c);
            reconcileStatusLocked(c, obs.event);
            pending.record(obs.mac, wasPublishable, isPublishable(c));
        }
        sink = sink_;
    }
    deliverPending(sink, pending);
}

void ObservationModelEngine::onLldpObservation(const LldpObservation& obs) {
    IModelEventSink* sink = nullptr;
    PendingEvents pending;
    {
        std::scoped_lock lock(mutex_);
        if (!interfacePolicy_->allowLldpEvidence(obs.localIfname)) return;

        const std::string candidateKey =
            resolveLldpIdentity(obs.remoteChassisId, obs.remotePortId);
        if (candidateKey.empty()) return;

        auto& c = getOrCreateCandidate(candidateKey);
        const bool wasPublishable = isPublishable(c);

        if (obs.event == ObservationEvent::Present) {
            c.seenInLldp        = true;
            c.remoteChassisId   = obs.remoteChassisId;
            c.remotePortId      = obs.remotePortId;
            c.remoteSystemName  = obs.remoteSystemName;
            c.lastSeen          = obs.observedAt;
            c.neighborIfaces.insert(obs.localIfname);
        } else {
            c.seenInLldp        = false;
            c.remoteChassisId.reset();
            c.remotePortId.reset();
            c.remoteSystemName.reset();
            c.neighborIfaces.erase(obs.localIfname);
        }

        if (hasNoEvidence(c)) {
            candidates_.erase(candidateKey);
            pending.recordErased(candidateKey, wasPublishable);
        } else {
            updateClassification(c);
            reconcileStatusLocked(c, obs.event);
            pending.record(candidateKey, wasPublishable, isPublishable(c));
        }
        sink = sink_;
    }
    deliverPending(sink, pending);
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

LocalNetworkSnapshot ObservationModelEngine::localSnapshot() const {
    std::scoped_lock lock(mutex_);
    return localState_.snapshot();
}

std::vector<RemoteCandidate> ObservationModelEngine::remoteCandidates() const {
    std::scoped_lock lock(mutex_);
    std::vector<RemoteCandidate> out;
    for (const auto& [_, c] : candidates_)
        if (isPublishable(c)) out.push_back(c);
    return out;
}

std::optional<RemoteCandidate> ObservationModelEngine::findCandidateByMac(const std::string& mac) const {
    std::scoped_lock lock(mutex_);
    auto it = candidates_.find(mac);
    if (it != candidates_.end() && isPublishable(it->second)) return it->second;
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Aging and lifecycle
// ---------------------------------------------------------------------------

void ObservationModelEngine::age(std::chrono::steady_clock::time_point now) {
    IModelEventSink* sink = nullptr;
    PendingEvents pending;
    {
        std::scoped_lock lock(mutex_);
        for (auto& [mac, c] : candidates_) {
            // Skip Provisional: restart window between prepareForRestart and
            // markLive must not flip preserved-lastSeen candidates to Aged.
            if (c.status == CandidateStatus::Removed ||
                c.status == CandidateStatus::Provisional) continue;

            const auto oldStatus      = c.status;
            const bool wasPublishable = isPublishable(c);

            const auto elapsed = now - c.lastSeen;
            if (elapsed > config_.candidateExpire)      c.status = CandidateStatus::Expired;
            else if (elapsed > config_.candidateAgeout) c.status = CandidateStatus::Aged;

            if (c.status == oldStatus) continue;
            pending.record(mac, wasPublishable, isPublishable(c));
        }
        sink = sink_;
    }
    deliverPending(sink, pending);
}

void ObservationModelEngine::prepareForRestart() {
    IModelEventSink* sink = nullptr;
    PendingEvents pending;
    {
        std::scoped_lock lock(mutex_);
        phase_ = ModelPhase::Initializing;
        localState_.clear();

        for (auto& [mac, c] : candidates_) {
            pending.recordErased(mac, isPublishable(c));

            c.neighborEvidence.clear();
            c.fdbEvidence.clear();
            c.seenInNeigh = c.seenInFdb = c.seenInLldp = false;
            c.ipv4.clear();
            c.ipv6.clear();
            c.bridgePort.reset();
            c.neighborIfaces.clear();
            c.remoteChassisId.reset();
            c.remotePortId.reset();
            c.remoteSystemName.reset();
            c.classification = CandidateClassification::Unknown;
            c.status = CandidateStatus::Provisional;
            c.score = {};
            // firstSeen/lastSeen deliberately preserved — aging continuity.
        }
        sink = sink_;
    }
    deliverPending(sink, pending);
}

void ObservationModelEngine::markLive() {
    IModelEventSink* sink = nullptr;
    PendingEvents pending;
    {
        std::scoped_lock lock(mutex_);
        phase_ = ModelPhase::Live;
        for (auto it = candidates_.begin(); it != candidates_.end();) {
            auto& [mac, c] = *it;
            if (c.status != CandidateStatus::Provisional) { ++it; continue; }

            updateClassification(c);
            if (isConfirmedWorthy(c.classification)) {
                c.status = CandidateStatus::Confirmed;
                pending.changed.insert(mac);
                ++it;
            } else {
                // Erase, not tombstone: a Removed candidate could never be
                // revived, permanently hiding a device that reappears later.
                it = candidates_.erase(it);
            }
        }
        sink = sink_;
    }
    deliverPending(sink, pending);
}

// ---------------------------------------------------------------------------
// Runtime config
// ---------------------------------------------------------------------------

void ObservationModelEngine::setInterfacePolicy(std::unique_ptr<IInterfacePolicy> policy) {
    std::scoped_lock lock(mutex_);
    interfacePolicy_ = std::move(policy);
}

void ObservationModelEngine::setClassifier(std::unique_ptr<ICandidateClassifier> classifier) {
    std::scoped_lock lock(mutex_);
    classifier_ = std::move(classifier);
    for (auto& [_, c] : candidates_) {
        updateClassification(c);
    }
}

// ---------------------------------------------------------------------------
// Internals
// ---------------------------------------------------------------------------

void ObservationModelEngine::reconcileAffectedByLocalMac(std::string_view mac) {
    reconcileByKey(mac);
}

void ObservationModelEngine::reconcileAffectedByLocalIp(const std::string& ip) {
    for (auto& [_, c] : candidates_) {
        if (c.ipv4.contains(ip) || c.ipv6.contains(ip)) {
            c.classification = CandidateClassification::LocalSelf;
            c.status = CandidateStatus::Removed;
        }
    }
}

void ObservationModelEngine::reconcileByKey(std::string_view mac) {
    auto it = candidates_.find(std::string(mac));
    if (it != candidates_.end() && localState_.isLocalMac(mac)) {
        it->second.classification = CandidateClassification::LocalSelf;
        it->second.status = CandidateStatus::Removed;
    }
}

RemoteCandidate& ObservationModelEngine::getOrCreateCandidate(const std::string& mac) {
    auto [it, inserted] = candidates_.try_emplace(mac);
    if (inserted) {
        it->second.mac = mac;
        it->second.firstSeen = std::chrono::steady_clock::now();
    }
    return it->second;
}

bool ObservationModelEngine::isPublishable(const RemoteCandidate& c) {
    if (c.status != CandidateStatus::Confirmed && c.status != CandidateStatus::Aged)
        return false;
    switch (c.classification) {
        case CandidateClassification::Artifact:
        case CandidateClassification::LocalSelf:
        case CandidateClassification::Unknown: return false;
        default: return true;
    }
}

void ObservationModelEngine::updateClassification(RemoteCandidate& c) {
    c.classification = classifier_->classify(c);
}

void ObservationModelEngine::reconcileStatusLocked(RemoteCandidate& c, ObservationEvent event) {
    if (event == ObservationEvent::Present) {
        if (c.status == CandidateStatus::Aged || c.status == CandidateStatus::Expired) {
            c.status = CandidateStatus::Confirmed;     // revival
        } else if (c.status == CandidateStatus::Removed) {
            c.status = CandidateStatus::Provisional;   // tombstone revival: re-validate
        }
    }

    if (phase_ == ModelPhase::Initializing) {
        if (c.status != CandidateStatus::Removed)
            c.status = CandidateStatus::Provisional;
    } else if (c.status == CandidateStatus::Provisional &&
               isConfirmedWorthy(c.classification)) {
        c.status = CandidateStatus::Confirmed;         // live-phase promotion
    }
}

} // namespace RSCGroup
