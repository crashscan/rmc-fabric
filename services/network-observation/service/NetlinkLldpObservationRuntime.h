#pragma once

#include "IObservationRuntime.h"
#include "BoundedObservationQueue.h"
#include "ObservationItem.h"

#include <ManagedWorker.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

#include "ObservationTypes.h"

namespace RSCGroup {
class INetworkObservationModel;
class LldpObserver;
class ILldpSource;
class ModelConfig;
class NetlinkNetworkMonitor;
struct MonitorCallbacks;

/**
 * Production observation runtime backed by netlink and LLDP inputs.
 *
 * The runtime coordinates input producers and delegates model behavior through
 * INetworkObservationModel. It does not expose engine, classifier, or policy
 * implementations.
 *
 * Threading notes:
 *  - Observations do NOT reach model_ on the producing thread. Netlink
 *    callbacks and LLDP watch callbacks push to observationQueue_; a single
 *    consumer worker drains it and is the ONLY thread that calls into
 *    model_. This is what stops a slow model update — or a slow transport
 *    behind it — from back-pressuring netlink parsing.
 *  - lldpObserver_ is atomically shared: netlink callbacks (monitor thread)
 *    load it per link event, while tick() (service aging thread) may create
 *    and publish a new observer after a retry. An observer is published only
 *    after a successful start(); a live observer is never replaced.
 *  - monitor_ is written only on the service thread in start()/stop(), which
 *    the service guarantees run outside the aging worker's lifetime (the
 *    worker is started after runtime start and joined before runtime stop).
 *    tick() never touches monitor_.
 *
 * Member destruction order: monitor_ is declared last so it is destroyed
 * first — its callbacks capture `this` and access model_ and lldpObserver_.
 */
class NetlinkLldpObservationRuntime final : public IObservationRuntime {
public:
    explicit NetlinkLldpObservationRuntime(ModelConfig config);

    /**
     * @param model     Injected model.
     * @param reassertInterval Keepalive period. Callers using this ctor must
     *        pass a value consistent with their model's candidateAgeout — it
     *        cannot be derived here because ModelConfig is not visible.
     *        Defaults to the ModelConfig default (candidateAgeout 60s / 2).
     */
    explicit NetlinkLldpObservationRuntime(
        std::unique_ptr<INetworkObservationModel> model,
        std::chrono::steady_clock::duration reassertInterval = std::chrono::seconds{30});

    /// Factory for LLDP sources; receives the model-bound downstream callback.
    /// Spelled without inputs/lldp headers on purpose: lldp-observer is linked
    /// PRIVATE into the service target and must not leak into this header.
    using LldpSourceFactory = std::function<std::unique_ptr<ILldpSource>(std::function<void(const LldpObservation &)>)>;

    ~NetlinkLldpObservationRuntime() override;

    NetlinkLldpObservationRuntime(const NetlinkLldpObservationRuntime &) = delete;

    NetlinkLldpObservationRuntime &operator=(const NetlinkLldpObservationRuntime &) = delete;

    [[nodiscard]] bool start() override;

    void stop() override;

    [[nodiscard]] bool isRunning() const override;

    [[nodiscard]] ObservationRuntimeHealth health() const override;

    void setInterfacePolicy(std::unique_ptr<IInterfacePolicy> policy) override;

    void setEventSink(IModelEventSink *sink) override;

    void setClassifier(std::unique_ptr<ICandidateClassifier> classifier) override;

    [[nodiscard]] LocalNetworkSnapshot localSnapshot() const override;

    [[nodiscard]] std::vector<RemoteCandidate> remoteCandidates() const override;

    [[nodiscard]] std::optional<RemoteCandidate> findCandidateByMac(const std::string &mac) const override;

    void age(std::chrono::steady_clock::time_point now) override;

    void tick(std::chrono::steady_clock::time_point now) override;

    /**
     * @brief Test seam: replace the LLDP source factory.
     *
     * Precondition: the runtime is not started. Enforced, because
     * createLldpObserver() runs on the supervision thread on every retry and
     * a late assignment would be a data race.
     *
     * Applies to the initial observer and to every tick()-driven retry.
     */
    void setLldpSourceFactoryForTest(LldpSourceFactory factory);

    /// Test seam: build the monitor callbacks without starting a monitor.
    /// Lets a test fire link events directly and observe that they reach an
    /// observer created after the callbacks were built. Not for production use.
    [[nodiscard]] MonitorCallbacks makeCallbacksForTest();

private:
    [[nodiscard]] std::shared_ptr<LldpObserver> createLldpObserver();

    [[nodiscard]] MonitorCallbacks makeCallbacks();

    void observationLoop(std::stop_token st);

    /// Applies one item to the model. Consumer thread only.
    void applyObservation(const ObservationItem &item);

    /// Unwinds a partial start; see the definition for ordering.
    void cleanUpFailedStart();

    void onObservationWorkerExit(const ManagedWorker::Exit &exit);

    void superviseLldp(std::chrono::steady_clock::time_point now);

    void reassertLldpNeighbors(std::chrono::steady_clock::time_point now);

    /**
     * @brief Repair model divergence after the observation queue dropped items.
     *
     * On the supervision tick, not the consumer loop: a redump is five netlink
     * round-trips, and a consumer blocked repairing is a consumer not draining —
     * which backs the queue up and drops more, raising the very bit it is trying
     * to clear. The tick cadence also supplies the retry throttle for free.
     */
    void performResync(std::chrono::steady_clock::time_point /*now*/);

    /// Keepalive period, derived from ModelConfig::candidateAgeout.
    std::chrono::steady_clock::duration reassertInterval_{std::chrono::seconds{30}};

    /// Creates LLDP sources. Defaults to the real lldpd-backed source;
    /// replaced by setLldpSourceFactoryForTest() before start().
    /// Read on the supervision thread by createLldpObserver() on every
    /// retry, which is why assignment is restricted to the pre-start window.
    LldpSourceFactory lldpSourceFactory_;

    std::unique_ptr<INetworkObservationModel> model_;
    std::atomic<std::shared_ptr<LldpObserver> > lldpObserver_{nullptr};

    /// Producer → model hand-off. Declared before the workers: the consumer
    /// loop and the netlink callbacks both touch it.
    BoundedObservationQueue observationQueue_;

    std::atomic<bool> observationWorkerFailed_{false};

    // LLDP supervision state — touched only on the tick() thread.
    std::chrono::steady_clock::time_point lastReassert_{};
    std::chrono::steady_clock::time_point lastLldpAttempt_{};
    std::chrono::steady_clock::time_point lastLldpProbe_{};
    unsigned lldpRetryCount_ = 0;

    /// Drains observationQueue_ into model_. Declared before monitor_ so
    /// monitor_ (and its callbacks) are destroyed first — but see stop(),
    /// which orders teardown explicitly rather than relying on this.
    ManagedWorker observationWorker_;

    // Must remain last: destroyed first. Its callbacks capture `this`.
    std::unique_ptr<NetlinkNetworkMonitor> monitor_;
};
} // namespace RSCGroup
