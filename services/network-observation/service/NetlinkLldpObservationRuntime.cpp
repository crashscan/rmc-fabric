#include "NetlinkLldpObservationRuntime.h"

#include "LldpObserver.h"
#include "LldpdSource.h"
#include "NetlinkNetworkMonitor.h"
#include "NetlinkTypes.h"
#include "ICandidateClassifier.h"
#include "INetworkObservationModel.h"
#include "ModelConfig.h"
#include "NetworkObservationModelFactory.h"

#include <glog/logging.h>
#include <algorithm>
#include <chrono>
#include <stdexcept>

#include "NetlinkObservationMapper.h"

namespace RSCGroup {
namespace {
    constexpr auto kLldpRetryInterval = std::chrono::seconds{30};
    constexpr auto kLldpProbeInterval = std::chrono::seconds{60};

    /// Keepalive period for LLDP re-assertion. Must stay below candidateAgeout
    /// (default 60s) so stable LLDP-only candidates are never aged out.
    std::chrono::steady_clock::duration deriveReassertInterval(std::chrono::seconds candidateAgeout) {
        using namespace std::chrono;
        return std::clamp(candidateAgeout / 2, seconds{10}, seconds{300});
    }
    /// Default: the real lldpd-backed source.
    NetlinkLldpObservationRuntime::LldpSourceFactory defaultLldpSourceFactory() {
        return [](std::function<void(const LldpObservation&)> downstream) {
            return std::make_unique<LldpdSource>(LldpSourceConfig{},
                                                 std::move(downstream));
        };
    }
} // namespace

MonitorCallbacks NetlinkLldpObservationRuntime::makeCallbacksForTest() {
        return makeCallbacks();
}

NetlinkLldpObservationRuntime::NetlinkLldpObservationRuntime(ModelConfig config)
    : reassertInterval_(deriveReassertInterval(config.candidateAgeout))
    , lldpSourceFactory_(defaultLldpSourceFactory())
    , model_(createNetworkObservationModel(std::move(config))) {
}

NetlinkLldpObservationRuntime::NetlinkLldpObservationRuntime(
    std::unique_ptr<INetworkObservationModel> model,
    std::chrono::steady_clock::duration reassertInterval)
    // Previously omitted, silently leaving the 30s default regardless of the
    // model's candidateAgeout — the exact ageout bug this work exists to fix,
    // on the ctor the tests use.
    : reassertInterval_(reassertInterval)
    , lldpSourceFactory_(defaultLldpSourceFactory())
    , model_(std::move(model)) {
}

NetlinkLldpObservationRuntime::~NetlinkLldpObservationRuntime() = default;

void NetlinkLldpObservationRuntime::setLldpSourceFactoryForTest(LldpSourceFactory factory) {
    if (isRunning()) {
        throw std::logic_error(
            "setLldpSourceFactoryForTest: must be called before start()");
    }
    lldpSourceFactory_ = std::move(factory);
}

std::shared_ptr<LldpObserver> NetlinkLldpObservationRuntime::createLldpObserver() {
    auto source = lldpSourceFactory_([this](const LldpObservation& obs) {
        model_->onLldpObservation(obs);
    });
    return std::make_shared<LldpObserver>(std::move(source));
}

MonitorCallbacks NetlinkLldpObservationRuntime::makeCallbacks() {
    ObservationSinks sinks;
    sinks.onLink     = [this](LinkObservation o)     { model_->onLinkObservation(o); };
    sinks.onAddress  = [this](AddressObservation o)  { model_->onAddressObservation(o); };
    sinks.onNeighbor = [this](NeighborObservation o) { model_->onNeighborObservation(o); };
    sinks.onFdb      = [this](FdbObservation o)      { model_->onFdbObservation(o); };
    sinks.onLinkStateForLldp = [this](const std::string& ifname, bool up) {
        if (auto observer = lldpObserver_.load()) {
            up ? observer->onInterfaceUp(ifname) : observer->onInterfaceDown(ifname);
        }
    };
    return RSCGroup::makeCallbacks(std::move(sinks));
}

bool NetlinkLldpObservationRuntime::start() {
    model_->prepareForRestart();

    if (auto observer = createLldpObserver(); observer->start()) {
        // Published only after a successful start; never replaced while live.
        lldpObserver_.store(std::move(observer));
    } else {
        LOG(WARNING) << "LLDP observer failed to start — LLDP unavailable (tick() will retry)";
    }

    monitor_ = std::make_unique<NetlinkNetworkMonitor>(makeCallbacks());
    if (!monitor_->start()) {
        lldpObserver_.store(nullptr);
        return false;
    }

    model_->markLive();

    LOG(INFO) << "NetlinkLldpObservationRuntime started";
    return true;
}

void NetlinkLldpObservationRuntime::stop() {
    // Ordering: stop netlink monitor first (its callbacks may call into LLDP),
    // then drain LLDP, then detach the event sink.  This ensures no
    // IModelEventSink callback fires after stop() returns.

    if (monitor_) {
        monitor_->stop();
        monitor_.reset();
    }
    if (auto observer = lldpObserver_.load()) {
        lldpObserver_.store(nullptr);
        observer->stop(); // drains in-flight LLDP callbacks
    }

    // Detach event sink only after all producers have drained.
    model_->setEventSink(nullptr);
}

bool NetlinkLldpObservationRuntime::isRunning() const {
    return monitor_ && monitor_->isRunning();
}

ObservationRuntimeHealth NetlinkLldpObservationRuntime::health() const {
    ObservationRuntimeHealth result;
    result.running = monitor_ && monitor_->isRunning();
    auto observer = lldpObserver_.load();
    result.lldpAvailable = observer && observer->isRunning();
    return result;
}

void NetlinkLldpObservationRuntime::setEventSink(IModelEventSink *sink) {
    model_->setEventSink(sink);
}

void NetlinkLldpObservationRuntime::setInterfacePolicy(std::unique_ptr<IInterfacePolicy> policy) {
    model_->setInterfacePolicy(std::move(policy));
}

void NetlinkLldpObservationRuntime::setClassifier(std::unique_ptr<ICandidateClassifier> classifier) {
    model_->setClassifier(std::move(classifier));
}

LocalNetworkSnapshot NetlinkLldpObservationRuntime::localSnapshot() const {
    return model_->localSnapshot();
}

std::vector<RemoteCandidate> NetlinkLldpObservationRuntime::remoteCandidates() const {
    return model_->remoteCandidates();
}

std::optional<RemoteCandidate> NetlinkLldpObservationRuntime::findCandidateByMac(const std::string &mac) const {
    return model_->findCandidateByMac(mac);
}

void NetlinkLldpObservationRuntime::age(std::chrono::steady_clock::time_point now) {
    model_->age(now);
}

void NetlinkLldpObservationRuntime::tick(std::chrono::steady_clock::time_point now) {
    superviseLldp(now);
    reassertLldpNeighbors(now);
}

void NetlinkLldpObservationRuntime::superviseLldp(std::chrono::steady_clock::time_point now) {
    auto observer = lldpObserver_.load();

    if (observer && !observer->isRunning()) {
        // A watchdog reconnect left the source stopped — drop it and fall
        // through to the retry path.
        LOG(WARNING) << "LLDP observer stopped — scheduling re-acquire";
        lldpObserver_.store(nullptr);
        observer.reset();
    }

    if (observer) {
        // Liveness probe. Watch silence is NOT a liveness signal: lldpd only
        // notifies on changes, so a stable network is legitimately silent.
        if (now - lastLldpProbe_ >= kLldpProbeInterval) {
            lastLldpProbe_ = now;
            if (!observer->isBackendAlive()) {
                LOG(WARNING) << "LLDP backend unreachable — reconnecting";
                observer->refreshAll(); // on failure source stops; dropped next tick
            }
        }
        return;
    }

    // No observer (initial start failure or teardown above): throttled retry.
    // A failed candidate is destroyed on this thread and was never published,
    // so no other thread can observe it.
    if (now - lastLldpAttempt_ < kLldpRetryInterval) {
        return;
    }
    lastLldpAttempt_ = now;

    auto candidate = createLldpObserver();
    if (!candidate->start()) {
        ++lldpRetryCount_;
        LOG(WARNING) << "LLDP observer retry " << lldpRetryCount_ << " failed";
        return;
    }

    lldpObserver_.store(std::move(candidate));
    LOG(INFO) << "LLDP observer acquired after " << lldpRetryCount_ << " failed attempt(s)";
    lldpRetryCount_ = 0;
}

void NetlinkLldpObservationRuntime::reassertLldpNeighbors(std::chrono::steady_clock::time_point now) {
    auto observer = lldpObserver_.load();
    if (!observer || !observer->isRunning()) {
        return;
    }
    if (now - lastReassert_ < reassertInterval_) {
        return;
    }
    lastReassert_ = now;
    observer->reassertAll();
}
} // namespace RSCGroup