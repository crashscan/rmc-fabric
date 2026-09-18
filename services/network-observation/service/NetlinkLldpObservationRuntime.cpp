#include "NetlinkLldpObservationRuntime.h"

#include "LldpObserver.h"
#include "LldpdSource.h"
#include "NetlinkNetworkMonitor.h"
#include "NetlinkTypes.h"
#include "ObservationItem.h"
#include "ICandidateClassifier.h"
#include "INetworkObservationModel.h"
#include "ModelConfig.h"
#include "NetworkObservationModelFactory.h"

#include <glog/logging.h>
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <type_traits>
#include <variant>

#include "NetlinkObservationMapper.h"

namespace RSCGroup {
namespace {
    /// Netlink bursts hard on boot and on bridge churn. Sized so a normal
    /// burst never drops; tune against stats().highWater on real hardware.
    constexpr std::size_t kObservationQueueCapacity = 4096;
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
    , model_(createNetworkObservationModel(std::move(config)))
    , observationQueue_(kObservationQueueCapacity)
    , observationWorker_("observation-consumer",
                         [this](std::stop_token st) { observationLoop(std::move(st)); },
                         // Wake, not close: close() is one-way and would
                         // leave the queue dead across a restart.
                         [this] { observationQueue_.wake(); },
                         [this](const ManagedWorker::Exit &exit) {
                             onObservationWorkerExit(exit);
                         }) {
}

NetlinkLldpObservationRuntime::NetlinkLldpObservationRuntime(
    std::unique_ptr<INetworkObservationModel> model,
    std::chrono::steady_clock::duration reassertInterval)
    // Previously omitted, silently leaving the 30s default regardless of the
    // model's candidateAgeout — the exact ageout bug this work exists to fix,
    // on the ctor the tests use.
    : reassertInterval_(reassertInterval)
    , lldpSourceFactory_(defaultLldpSourceFactory())
    , model_(std::move(model))
    , observationQueue_(kObservationQueueCapacity)
    , observationWorker_("observation-consumer",
                         [this](std::stop_token st) { observationLoop(std::move(st)); },
                         // Wake, not close: close() is one-way and would
                         // leave the queue dead across a restart.
                         [this] { observationQueue_.wake(); },
                         [this](const ManagedWorker::Exit &exit) {
                             onObservationWorkerExit(exit);
                         }) {
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
    auto source = lldpSourceFactory_([this](const LldpObservation &obs) {
        observationQueue_.push({ObservationSource::Lldp, obs});
    });
    return std::make_shared<LldpObserver>(std::move(source));
}

MonitorCallbacks NetlinkLldpObservationRuntime::makeCallbacks() {
    ObservationSinks sinks;
    // Hand off to the queue instead of calling into the model. This is the
    // boundary that keeps netlink parsing off the model's critical path.
    sinks.onLink = [this](LinkObservation o) {
        observationQueue_.push({ObservationSource::Netlink, std::move(o)});
    };
    sinks.onAddress = [this](AddressObservation o) {
        observationQueue_.push({ObservationSource::Netlink, std::move(o)});
    };
    sinks.onNeighbor = [this](NeighborObservation o) {
        observationQueue_.push({ObservationSource::Netlink, std::move(o)});
    };
    sinks.onFdb = [this](FdbObservation o) {
        observationQueue_.push({ObservationSource::Netlink, std::move(o)});
    };

    // NOT queued. This is a control signal, not an observation: it drives
    // LldpObserver's interface flush and must stay ordered with respect to
    // the link event that caused it (see the mapper's ordering test).
    // Routing it through the queue would let the flush overtake or lag the
    // link change it belongs to.
    sinks.onLinkStateForLldp = [this](const std::string &ifname, bool up) {
        if (auto observer = lldpObserver_.load()) {
            up ? observer->onInterfaceUp(ifname) : observer->onInterfaceDown(ifname);
        }
    };
    return RSCGroup::makeCallbacks(std::move(sinks));
}

/**
 * @brief Unwind a partial start in the same order as stop().
 *
 * Producers first, then close, then join the consumer. Every step is
 * idempotent and safe on a component that was never started, so all
 * failure exits can share one path rather than each unwinding to a
 * different depth.
 */
void NetlinkLldpObservationRuntime::cleanUpFailedStart() {
    if (monitor_) {
        monitor_->stop();
        monitor_.reset();
    }
    if (auto observer = lldpObserver_.load()) {
        lldpObserver_.store(nullptr);
        observer->stop(); // drains in-flight LLDP callbacks
    }
    observationQueue_.close();
    observationWorker_.stop();
}

bool NetlinkLldpObservationRuntime::start() {
    if (monitor_) {
        LOG(ERROR) << "start() called while already started";
        return false;
    }

    model_->prepareForRestart();

    // Reopen and start the consumer BEFORE any producer. The initial netlink
    // dump fires callbacks during monitor_->start(); a closed or undrained
    // queue at that moment loses the entire boot snapshot and the model
    // starts empty — silently.
    observationQueue_.reopen();
    observationWorkerFailed_.store(false, std::memory_order_release);
    try {
        (void) observationWorker_.start();
    } catch (const std::exception &e) {
        LOG(ERROR) << "observation consumer failed to start: " << e.what();
        cleanUpFailedStart();
        return false;
    } catch (...) {
        LOG(ERROR) << "observation consumer failed to start: unknown exception";
        cleanUpFailedStart();
        return false;
    }

    // Not fatal: tick() retries. The observer is published only on success,
    // so a failed candidate is destroyed here and never observed elsewhere.
    if (auto observer = createLldpObserver(); observer->start()) {
        lldpObserver_.store(std::move(observer));
    } else {
        LOG(WARNING) << "LLDP observer failed to start — LLDP unavailable (tick() will retry)";
    }

    monitor_ = std::make_unique<NetlinkNetworkMonitor>(makeCallbacks());
    if (!monitor_->start()) {
        // Full unwind: the LLDP observer may already be running and pushing.
        // Detaching it without stop() would leave a live watch thread
        // producing into a closed queue.
        cleanUpFailedStart();
        return false;
    }

    model_->markLive();
    LOG(INFO) << "NetlinkLldpObservationRuntime started";
    return true;
}

void NetlinkLldpObservationRuntime::stop() {
    // Ordering: silence producers, then close, then join, then detach the
    // sink. Closing before the producers are stopped would drop observations
    // they are still emitting; joining before closing would leave the
    // consumer parked with a full queue.
    if (monitor_) {
        monitor_->stop();
        monitor_.reset();
    }
    if (auto observer = lldpObserver_.load()) {
        lldpObserver_.store(nullptr);
        observer->stop(); // drains in-flight LLDP callbacks
    }

    // No producer can push from here. Close so the consumer drains what is
    // already queued, then join.
    observationQueue_.close();
    observationWorker_.stop();

    // Detach event sink only after all producers AND the consumer have
    // drained — the consumer is itself a producer of model events.
    model_->setEventSink(nullptr);
}

bool NetlinkLldpObservationRuntime::isRunning() const {
    // A dead consumer means observations no longer reach the model, so the
    // runtime is not running even though the monitor is healthy. Kept here
    // rather than only in health() so every caller — including
    // setLldpSourceFactoryForTest's guard — sees one answer.
    if (observationWorkerFailed_.load(std::memory_order_acquire)) {
        return false;
    }
    return monitor_ && monitor_->isRunning();
}

ObservationRuntimeHealth NetlinkLldpObservationRuntime::health() const {
    ObservationRuntimeHealth result;
    result.running = isRunning();
    auto observer = lldpObserver_.load();
    result.lldpAvailable = observer && observer->isRunning();
    return result;
}

void NetlinkLldpObservationRuntime::observationLoop(std::stop_token st) {
    // waitPop returns nullopt only when closed AND drained, so a stop during
    // shutdown still delivers everything already queued.
    while (auto item = observationQueue_.waitPop(st)) {
        applyObservation(*item);
    }
}

void NetlinkLldpObservationRuntime::applyObservation(const ObservationItem &item) {
    // Exhaustive by construction: adding a variant alternative without a
    // branch here fails to compile.
    std::visit([this](const auto &o) {
        using T = std::decay_t<decltype(o)>;
        if constexpr (std::is_same_v<T, LinkObservation>) {
            model_->onLinkObservation(o);
        } else if constexpr (std::is_same_v<T, AddressObservation>) {
            model_->onAddressObservation(o);
        } else if constexpr (std::is_same_v<T, NeighborObservation>) {
            model_->onNeighborObservation(o);
        } else if constexpr (std::is_same_v<T, FdbObservation>) {
            model_->onFdbObservation(o);
        } else if constexpr (std::is_same_v<T, LldpObservation>) {
            model_->onLldpObservation(o);
        } else {
            static_assert(!sizeof(T *), "unhandled ObservationItem alternative");
        }
    }, item.payload);
}

void NetlinkLldpObservationRuntime::onObservationWorkerExit(const ManagedWorker::Exit &exit) {
    if (exit.reason != ManagedWorker::ExitReason::exception) return;
    std::string detail = "unknown exception";
    try {
        if (exit.exception) std::rethrow_exception(exit.exception);
    } catch (const std::exception &e) { detail = e.what(); } catch (...) {}

    // No issue-reporting channel here — the runtime has no diagnostics sink.
    // health() reflects it instead: see below.
    LOG(ERROR) << "observation consumer worker terminated: " << detail;
    observationWorkerFailed_.store(true, std::memory_order_release);
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
    performResync(now);
}

/**
 * @brief Repair model divergence after the observation queue dropped items.
 *
 * On the supervision tick, not the consumer loop: a redump is five netlink
 * round-trips, and a consumer blocked repairing is a consumer not draining —
 * which backs the queue up and drops more, raising the very bit it is trying
 * to clear. The tick cadence also supplies the retry throttle for free.
 */
void NetlinkLldpObservationRuntime::performResync(std::chrono::steady_clock::time_point /*now*/) {
    const auto mask = observationQueue_.takeResyncMask();
    if (mask == 0) return;

    if (mask & sourceBit(ObservationSource::Netlink)) {
        // Drop the backlog first: it predates the redump and re-applying it
        // afterwards would overwrite fresh kernel state with stale events.
        observationQueue_.discardAll();

        if (!monitor_ || !monitor_->requestRedump()) {
            observationQueue_.raiseResync(ObservationSource::Netlink);
            LOG(WARNING) << "netlink resync failed; retrying next tick";
        } else {
            LOG(INFO) << "netlink resync completed";
        }
    }

    if (mask & sourceBit(ObservationSource::Lldp)) {
        // refreshAll() reconnects, re-enumerates, and — unlike the netlink
        // redump — reconciles removals via reconcileAfterRefresh(). On
        // failure the source stops and superviseLldp() re-acquires it next
        // tick, so this path is self-healing and needs no re-raise.
        if (auto observer = lldpObserver_.load()) {
            observer->refreshAll();
        } else {
            LOG(WARNING) << "LLDP resync skipped: no observer; retry path owns recovery";
        }
    }
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