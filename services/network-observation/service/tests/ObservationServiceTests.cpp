#include "ObservationService.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace RSCGroup;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool waitFor(const std::function<bool()>& predicate,
             std::chrono::milliseconds timeout = std::chrono::milliseconds(500))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
}

class FakeObservationRuntime final : public IObservationRuntime {
public:
    explicit FakeObservationRuntime(bool startResult = true, bool lldpAvailable = true)
        : startResult_(startResult)
        , lldpAvailable_(lldpAvailable)
    {
    }

    void setEventSink(IModelEventSink* sink) override
    {
        sink_ = sink;
    }

    void setInterfacePolicy(std::unique_ptr<IInterfacePolicy>) override {}
    void setClassifier(std::unique_ptr<ICandidateClassifier>) override {}

    bool start() override
    {
        ++startCount_;
        running_ = startResult_;
        return startResult_;
    }

    void stop() override
    {
        std::scoped_lock lock(mutex_);
        ++stopCount_;
        stopSawActiveAge_ = ageActive_;
        running_ = false;
    }

    bool isRunning() const override
    {
        return running_.load();
    }

    ObservationRuntimeHealth health() const override
    {
        ObservationRuntimeHealth health;
        health.running = running_.load();
        health.lldpAvailable = lldpAvailable_.load();
        return health;
    }

    LocalNetworkSnapshot localSnapshot() const override
    {
        return {};
    }

    std::vector<RemoteCandidate> remoteCandidates() const override
    {
        return {};
    }

    std::optional<RemoteCandidate> findCandidateByMac(const std::string&) const override
    {
        return std::nullopt;
    }

    void age(std::chrono::steady_clock::time_point) override
    {
        if (onAge_) {
            onAge_();
        }
        if (throwOnAge_.load()) {
            throw std::runtime_error("age failed");
        }
        std::unique_lock lock(mutex_);
        ageActive_ = true;
        ageEntered_ = true;
        ageEnteredCv_.notify_all();
        if (blockInAge_) {
            releaseAgeCv_.wait(lock, [&] { return releaseAge_; });
        }
        ageActive_ = false;
    }

    void waitUntilAgeEntered()
    {
        std::unique_lock lock(mutex_);
        ageEnteredCv_.wait(lock, [&] { return ageEntered_; });
    }

    void releaseAge()
    {
        std::scoped_lock lock(mutex_);
        releaseAge_ = true;
        releaseAgeCv_.notify_all();
    }

    [[nodiscard]] IModelEventSink* sink() const { return sink_; }
    [[nodiscard]] int startCount() const { return startCount_.load(); }
    [[nodiscard]] int stopCount() const { return stopCount_.load(); }
    [[nodiscard]] bool stopSawActiveAge() const { return stopSawActiveAge_.load(); }
    void setLldpAvailable(bool value) { lldpAvailable_.store(value); }
    void setRunning(bool value) { running_.store(value); }
    void setThrowOnAge(bool value) { throwOnAge_.store(value); }
    void setOnAge(std::function<void()> hook) { onAge_ = std::move(hook); }
    void setBlockInAge(bool value)
    {
        std::scoped_lock lock(mutex_);
        blockInAge_ = value;
    }

private:
    bool startResult_{true};
    IModelEventSink* sink_{nullptr};
    std::atomic<bool> running_{false};
    std::atomic<bool> lldpAvailable_{true};
    std::atomic<bool> throwOnAge_{false};
    std::function<void()> onAge_;
    std::atomic<int> startCount_{0};
    std::atomic<int> stopCount_{0};
    std::atomic<bool> stopSawActiveAge_{false};
    mutable std::mutex mutex_;
    bool ageActive_{false};
    bool ageEntered_{false};
    bool blockInAge_{false};
    bool releaseAge_{false};
    std::condition_variable ageEnteredCv_;
    std::condition_variable releaseAgeCv_;
};

class FakeObservationTransport final : public IObservationTransport {
public:
    explicit FakeObservationTransport(bool startResult = true,
                                      bool throwOnLocal = false,
                                      bool throwOnInterface = false,
                                      bool throwOnCandidate = false)
        : startResult_(startResult)
        , throwOnLocal_(throwOnLocal)
        , throwOnInterface_(throwOnInterface)
        , throwOnCandidate_(throwOnCandidate)
    {
    }

    void bindQueryService(IObservationQueryService& provider) override
    {
        provider_ = &provider;
    }

    bool start() override
    {
        ++startCount_;
        startSawBound_ = provider_ != nullptr;
        return startResult_;
    }

    void stop() override
    {
        ++stopCount_;
    }

    void quiesceQueries() noexcept override
    {
        ++quiesceCount_;
    }

    std::string name() const override
    {
        if (!name_.empty()) {
            return name_;
        }
        return startResult_ ? "fake" : "failing";
    }

    void setName(std::string name) { name_ = std::move(name); }

    void publishReadyChanged(bool ready) override
    {
        if (ready) {
            ++readyTrueCount_;
        } else {
            ++readyFalseCount_;
        }
    }

    void publishLocalStateChanged() override
    {
        ++localStateChangedCount_;
        if (throwOnLocal_) {
            throw std::runtime_error("publishLocalStateChanged failed");
        }
    }
    void publishInterfaceChanged(const std::string&) override
    {
        ++interfaceChangedCount_;
        if (throwOnInterface_) {
            throw std::runtime_error("publishInterfaceChanged failed");
        }
    }
    void publishInterfaceRemoved(const std::string&) override
    {
        ++interfaceRemovedCount_;
        if (throwOnInterface_) {
            throw std::runtime_error("publishInterfaceRemoved failed");
        }
    }
    void publishCandidateChanged(const std::string&) override
    {
        ++candidateChangedCount_;
        if (throwOnCandidate_) {
            throw std::runtime_error("publishCandidateChanged failed");
        }
    }
    void publishCandidateRemoved(const std::string&) override
    {
        ++candidateRemovedCount_;
        if (throwOnCandidate_) {
            throw std::runtime_error("publishCandidateRemoved failed");
        }
    }

    [[nodiscard]] bool startSawBound() const { return startSawBound_.load(); }
    [[nodiscard]] int startCount() const { return startCount_.load(); }
    [[nodiscard]] int stopCount() const { return stopCount_.load(); }
    [[nodiscard]] int readyTrueCount() const { return readyTrueCount_.load(); }
    [[nodiscard]] int readyFalseCount() const { return readyFalseCount_.load(); }
    [[nodiscard]] int localStateChangedCount() const { return localStateChangedCount_.load(); }
    [[nodiscard]] int interfaceChangedCount() const { return interfaceChangedCount_.load(); }
    [[nodiscard]] int interfaceRemovedCount() const { return interfaceRemovedCount_.load(); }
    [[nodiscard]] int candidateChangedCount() const { return candidateChangedCount_.load(); }
    [[nodiscard]] int candidateRemovedCount() const { return candidateRemovedCount_.load(); }
    [[nodiscard]] int quiesceCount() const { return quiesceCount_.load(); }

    void setThrowOnLocal(bool v) { throwOnLocal_.store(v); }
    void setThrowOnInterface(bool v) { throwOnInterface_.store(v); }
    void setThrowOnCandidate(bool v) { throwOnCandidate_.store(v); }

private:
    bool startResult_{true};
    std::atomic<bool> throwOnLocal_{false};
    std::atomic<bool> throwOnInterface_{false};
    std::atomic<bool> throwOnCandidate_{false};
    IObservationQueryService* provider_{nullptr};
    std::atomic<bool> startSawBound_{false};
    std::atomic<int> startCount_{0};
    std::atomic<int> stopCount_{0};
    std::atomic<int> readyTrueCount_{0};
    std::atomic<int> readyFalseCount_{0};
    std::atomic<int> localStateChangedCount_{0};
    std::atomic<int> interfaceChangedCount_{0};
    std::atomic<int> interfaceRemovedCount_{0};
    std::atomic<int> candidateChangedCount_{0};
    std::atomic<int> candidateRemovedCount_{0};
    std::atomic<int> quiesceCount_{0};
    std::string name_;
};

void testStartStopBindsTransportAndPublishesReadinessExactlyOnce()
{
    auto runtime = std::make_unique<FakeObservationRuntime>();
    auto runtimePtr = runtime.get();
    runtimePtr->setBlockInAge(true);
    auto transport = std::make_shared<FakeObservationTransport>();

    ObservationService service(std::move(runtime), transport, std::chrono::milliseconds(100));
    expect(service.getPhase() == "stopped", "phase should be stopped before start");
    expect(service.start(), "observation service should start");
    expect(runtimePtr->sink() != nullptr, "runtime should receive model event sink before start");
    expect(transport->startSawBound(), "transport should be bound before start");
    expect(transport->readyTrueCount() == 1, "ready=true should publish exactly once on start");
    expect(service.getPhase() == "live", "phase should be live after ready start");

    service.stop();
    service.stop();

    expect(runtimePtr->stopCount() == 1, "runtime stop must be idempotent via service stop");
    expect(transport->stopCount() == 1, "transport stop must be called exactly once");
    expect(transport->readyFalseCount() == 1, "ready=false should publish exactly once on stop");
    expect(service.getPhase() == "stopped", "phase should be stopped after stop");
}

void testTransportFailurePreventsRuntimeStart()
{
    auto runtime = std::make_unique<FakeObservationRuntime>();
    auto runtimePtr = runtime.get();
    auto failingTransport = std::make_shared<FakeObservationTransport>(false);

    ObservationService service(std::move(runtime), failingTransport, std::chrono::milliseconds(100));
    expect(!service.start(), "service start should fail when transport start fails");
    expect(runtimePtr->startCount() == 0, "runtime must not start if transport startup fails");
    expect(failingTransport->stopCount() == 1, "failing transport should be rolled back exactly once");
}

void testStopWaitsForAgingThreadBeforeStoppingRuntimeAndTransport()
{
    auto runtime = std::make_unique<FakeObservationRuntime>();
    auto runtimePtr = runtime.get();
    auto transport = std::make_shared<FakeObservationTransport>();
    runtimePtr->setBlockInAge(true);

    ObservationService service(std::move(runtime), transport, std::chrono::milliseconds(1));
    expect(service.start(), "service should start with fast aging interval");
    runtimePtr->waitUntilAgeEntered();

    auto stopFuture = std::async(std::launch::async, [&service] { service.stop(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    expect(runtimePtr->stopCount() == 0, "runtime stop must wait for the aging thread to exit");
    expect(transport->stopCount() == 0, "transport stop must wait for runtime-owned worker shutdown");
    expect(transport->quiesceCount() == 1, "query admission must be quiesced before the producer drain");
    expect(transport->readyFalseCount() == 0,
           "terminal ReadyChanged(false) must not be published before the producer drain");

    runtimePtr->releaseAge();
    stopFuture.get();

    expect(transport->readyFalseCount() == 1,
           "terminal ReadyChanged(false) should be published after the producer drain");
    expect(runtimePtr->stopCount() == 1, "runtime should stop after aging thread exits");
    expect(!runtimePtr->stopSawActiveAge(), "runtime stop must observe no active aging callback");
    expect(transport->stopCount() == 1, "transport should stop after runtime shutdown");
}

void testPublishFailureDoesNotBlockLaterTransports()
{
    auto runtime = std::make_unique<FakeObservationRuntime>();
    auto throwing = std::make_shared<FakeObservationTransport>(true, true, true, true);
    auto observing = std::make_shared<FakeObservationTransport>();
    observing->setName("observer");

    ObservationService service(std::move(runtime), throwing, std::chrono::milliseconds(100));
    service.addTransport(observing);

    expect(service.start(), "service should start with throwing publish transport");

    ModelEvent localChanged;
    localChanged.kind = ModelEventKind::LocalInterfaceChanged;
    localChanged.ifname = std::string("eth0");
    service.onModelEvent(localChanged);

    ModelEvent candidateChanged;
    candidateChanged.kind = ModelEventKind::CandidateUpdated;
    candidateChanged.mac = std::string("00:11:22:33:44:55");
    service.onModelEvent(candidateChanged);

    expect(observing->interfaceChangedCount() == 1, "later transport should still receive interface change");
    expect(observing->localStateChangedCount() == 1, "later transport should still receive local-state change");
    expect(observing->candidateChangedCount() == 1, "later transport should still receive candidate change");
    expect(waitFor([&] {
        return service.getIssues().contains("observation.transport.fake.publish_interface_changed.failed")
            || service.getIssues().contains("observation.transport.fake.publish_local_state_changed.failed")
            || service.getIssues().contains("observation.transport.fake.publish_candidate_changed.failed");
    }), "transport publish failure should surface as an issue");
    service.stop();
}

void testAddTransportAfterStartIsRejected()
{
    auto runtime = std::make_unique<FakeObservationRuntime>();
    auto transport = std::make_shared<FakeObservationTransport>();
    ObservationService service(std::move(runtime), transport, std::chrono::milliseconds(100));

    expect(service.start(), "service should start before addTransport rejection");

    bool threw = false;
    try {
        service.addTransport(std::make_shared<FakeObservationTransport>());
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "addTransport after start should be rejected");
    service.stop();
}

void testReadinessIsIndependentFromRuntimeIssuesAndIssuesResetOnRestart()
{
    auto runtime = std::make_unique<FakeObservationRuntime>(true, false);
    auto runtimePtr = runtime.get();
    auto transport = std::make_shared<FakeObservationTransport>();

    ObservationService service(std::move(runtime), transport, std::chrono::milliseconds(1));
    expect(service.start(), "service should start with degraded LLDP health");
    expect(service.isReady(), "service readiness should remain true while runtime is degraded");
    expect(waitFor([&] {
        const auto issues = service.getIssues();
        return issues.contains("observation.input.lldp.unavailable");
    }), "lldp degradation should surface through issues");

    service.stop();
    expect(service.getIssues().empty(), "stop should clear runtime issues for restart");

    runtimePtr->setLldpAvailable(true);
    expect(service.start(), "service should restart after stop");
    expect(waitFor([&] {
        return service.getIssues().empty();
    }), "issues should stay clear after recovery");
    service.stop();
}

void testAgingLoopFailureSurfacesIssueWithoutClearingReadiness()
{
    auto runtime = std::make_unique<FakeObservationRuntime>();
    auto runtimePtr = runtime.get();
    runtimePtr->setThrowOnAge(true);
    auto transport = std::make_shared<FakeObservationTransport>();

    ObservationService service(std::move(runtime), transport, std::chrono::milliseconds(1));
    expect(service.start(), "service should start before aging failure");
    expect(waitFor([&] {
        return service.getIssues().contains("observation.worker.aging.stopped");
    }), "aging loop failure should surface through issues");
    expect(service.isReady(), "aging loop failure should not silently clear readiness");
    service.stop();
}

void testOperationScopedIssueCodesPreserveInterfaceFailureAfterLocalStateSuccess()
{
    // Verifies that a successful publishLocalStateChanged does not erase the
    // issue created by a prior publishInterfaceChanged failure on the same
    // transport (they use separate operation-scoped issue codes).
    auto runtime = std::make_unique<FakeObservationRuntime>();
    auto transport = std::make_shared<FakeObservationTransport>(true, false, true, false);

    ObservationService service(std::move(runtime), transport, std::chrono::milliseconds(100));
    expect(service.start(), "service should start");

    ModelEvent ev;
    ev.kind = ModelEventKind::LocalInterfaceChanged;
    ev.ifname = std::string("eth0");
    service.onModelEvent(ev);

    // publishInterfaceChanged throws -> issue recorded.
    // publishLocalStateChanged succeeds -> only that operation's issue is cleared.
    const auto interfaceIssueCode = "observation.transport.fake.publish_interface_changed.failed";
    const auto localStateIssueCode = "observation.transport.fake.publish_local_state_changed.failed";

    expect(waitFor([&] {
        return service.getIssues().contains(interfaceIssueCode);
    }), "interface-changed failure must remain even after local-state success");
    expect(!service.getIssues().contains(localStateIssueCode),
           "local-state issue must not be present when publish_local_state_changed succeeded");

    service.stop();
}

void testOperationScopedIssueClearedBySubsequentSuccess()
{
    auto runtime = std::make_unique<FakeObservationRuntime>();
    auto transport = std::make_shared<FakeObservationTransport>(true, false, true, false);

    ObservationService service(std::move(runtime), transport, std::chrono::milliseconds(100));
    expect(service.start(), "service should start");

    ModelEvent ev;
    ev.kind = ModelEventKind::LocalInterfaceChanged;
    ev.ifname = std::string("eth0");
    service.onModelEvent(ev);

    const auto interfaceIssueCode = "observation.transport.fake.publish_interface_changed.failed";
    expect(waitFor([&] {
        return service.getIssues().contains(interfaceIssueCode);
    }), "interface issue must be present after failure");

    // Recover: stop throwing on publishInterfaceChanged.
    transport->setThrowOnInterface(false);
    service.onModelEvent(ev);

    expect(waitFor([&] {
        return !service.getIssues().contains(interfaceIssueCode);
    }), "interface issue must be cleared after subsequent publishInterfaceChanged success");

    service.stop();
}

void testTwoOperationIssuesCanCoexist()
{
    // Both publishInterfaceChanged and publishLocalStateChanged throw.
    auto runtime = std::make_unique<FakeObservationRuntime>();
    auto transport = std::make_shared<FakeObservationTransport>(true, true, true, false);

    ObservationService service(std::move(runtime), transport, std::chrono::milliseconds(100));
    expect(service.start(), "service should start");

    ModelEvent ev;
    ev.kind = ModelEventKind::LocalInterfaceChanged;
    ev.ifname = std::string("eth0");
    service.onModelEvent(ev);

    const auto interfaceIssueCode = "observation.transport.fake.publish_interface_changed.failed";
    const auto localStateIssueCode = "observation.transport.fake.publish_local_state_changed.failed";

    const auto issues = service.getIssues();
    expect(waitFor([&] {
        const auto i = service.getIssues();
        return i.contains(interfaceIssueCode) && i.contains(localStateIssueCode);
    }), "both operation issues must be present when both publish calls throw");

    service.stop();
}

void testSelfStopFromAgingThreadIsRejected()
{
    auto runtime = std::make_unique<FakeObservationRuntime>();
    auto runtimePtr = runtime.get();
    auto transport = std::make_shared<FakeObservationTransport>();

    ObservationService service(std::move(runtime), transport, std::chrono::milliseconds(1));

    std::atomic<bool> selfStopAttempted{false};
    runtimePtr->setOnAge([&] {
        if (selfStopAttempted.exchange(true)) {
            return;
        }
        // Rejected before shutdown is claimed: no detach, no partial teardown.
        service.stop();
    });

    expect(service.start(), "service should start");
    expect(waitFor([&] { return selfStopAttempted.load(); }), "the aging worker should attempt a self stop");
    expect(service.isReady(), "a rejected self stop must not tear the service down");
    expect(transport->stopCount() == 0, "a rejected self stop must not close transports");
    expect(runtimePtr->stopCount() == 0, "a rejected self stop must not stop the runtime");

    service.stop();
    expect(transport->stopCount() == 1, "an external stop must still close transports exactly once");
    expect(runtimePtr->stopCount() == 1, "an external stop must still stop the runtime exactly once");
}

void testAgingWorkerCrashRemainsObservationOwnedDegradation()
{
    auto runtime = std::make_unique<FakeObservationRuntime>();
    auto runtimePtr = runtime.get();
    runtimePtr->setThrowOnAge(true);
    auto transport = std::make_shared<FakeObservationTransport>();

    ObservationService service(std::move(runtime), transport, std::chrono::milliseconds(1));
    expect(service.start(), "service should start");
    expect(waitFor([&] { return service.getIssues().contains("observation.worker.aging.stopped"); }),
           "aging worker crash should surface as an observation-owned issue");

    // Unlike inventory Policy A, observation treats an aging-worker crash as
    // degradation: readiness is unchanged and a repeated start() is a no-op.
    expect(service.isReady(), "aging worker crash must not clear readiness");
    expect(service.start(), "a repeated start() after an aging crash must not throw");
    expect(transport->startCount() == 1, "a repeated start() must not restart transports");

    service.stop();
    expect(service.getIssues().empty(), "stop() should clear runtime issue state");
}

void testConcurrentStopWaitsForTeardownCompletion()
{
    auto runtime = std::make_unique<FakeObservationRuntime>();
    auto runtimePtr = runtime.get();
    runtimePtr->setBlockInAge(true);
    auto transport = std::make_shared<FakeObservationTransport>();

    ObservationService service(std::move(runtime), transport, std::chrono::milliseconds(1));
    expect(service.start(), "service should start");
    runtimePtr->waitUntilAgeEntered();

    auto first = std::async(std::launch::async, [&service] { service.stop(); });
    auto second = std::async(std::launch::async, [&service] { service.stop(); });

    expect(second.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout ||
           first.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout,
           "a concurrent stop must not return while teardown is still active");

    runtimePtr->releaseAge();
    first.get();
    second.get();

    expect(transport->stopCount() == 1, "transports must be closed exactly once");
    expect(runtimePtr->stopCount() == 1, "the runtime must be stopped exactly once");
}

} // namespace

int main()
{
    testStartStopBindsTransportAndPublishesReadinessExactlyOnce();
    testTransportFailurePreventsRuntimeStart();
    testStopWaitsForAgingThreadBeforeStoppingRuntimeAndTransport();
    testPublishFailureDoesNotBlockLaterTransports();
    testAddTransportAfterStartIsRejected();
    testReadinessIsIndependentFromRuntimeIssuesAndIssuesResetOnRestart();
    testAgingLoopFailureSurfacesIssueWithoutClearingReadiness();
    testOperationScopedIssueCodesPreserveInterfaceFailureAfterLocalStateSuccess();
    testOperationScopedIssueClearedBySubsequentSuccess();
    testTwoOperationIssuesCanCoexist();
    testSelfStopFromAgingThreadIsRejected();
    testAgingWorkerCrashRemainsObservationOwnedDegradation();
    testConcurrentStopWaitsForTeardownCompletion();
    return EXIT_SUCCESS;
}
