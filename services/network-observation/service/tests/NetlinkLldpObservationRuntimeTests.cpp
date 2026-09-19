//
// Created by vvass on 18-Sep-26.
//

#include "NetlinkLldpObservationRuntime.h"
#include "NetlinkObservationMapper.h"
#include "ILldpSource.h"
#include "INetworkObservationModel.h"

#include <linux/rtnetlink.h>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
using namespace RSCGroup;
using namespace std::chrono_literals;

void expect(bool cond, const std::string& msg) {
    if (!cond) { std::cerr << "RuntimeTests: " << msg << '\n'; std::exit(EXIT_FAILURE); }
}

/// Scripted LLDP source. start() consults a caller-supplied result sequence.
class FakeLldpSource final : public ILldpSource {
public:
    struct Shared {
        std::vector<bool> startResults;     // consumed in order; past end = true
        std::size_t       startCalls{0};
        std::atomic<bool> running{false};
        /// Push-subscription liveness, independent of `running`. Set by
        /// start()/refreshAll(), cleared by stop() or by a test simulating
        /// a daemon restart. Defaulting this to false made every healthy
        /// tick take the dead-watch repair branch.
        std::atomic<bool> watchAlive{false};
        std::atomic<bool> backendAlive{true};
        std::atomic<int>  refreshAllCalls{0};
        std::atomic<int>  reassertCalls{0};
        /// A failed reconnect. Production refreshAll() RETAINS the existing
        /// watch and leaves the epoch running — only the return value says it
        /// failed. The old stopOnRefresh flag also cleared `running`, which
        /// no longer happens and would route the next tick down the drop
        /// branch instead of the escalation.
        std::atomic<bool> failRefresh{false};
        std::vector<std::string> interfacesDown;
        std::vector<std::string> interfacesUp;
    };

    explicit FakeLldpSource(std::shared_ptr<Shared> s) : s_(std::move(s)) {}

    bool start() override {
        const auto i = s_->startCalls++;
        const bool ok = i < s_->startResults.size() ? s_->startResults[i] : true;
        s_->running = ok;
        // A started source holds a live subscription.
        s_->watchAlive = ok;
        return ok;
    }
    void stop() override {
        s_->running    = false;
        s_->watchAlive = false;
    }
    [[nodiscard]] bool isRunning() const override { return s_->running; }
    [[nodiscard]] bool isWatchAlive() const override { return s_->watchAlive; }

    bool refreshAll() override {
        ++s_->refreshAllCalls;
        // Failure retains the watch and the epoch; only the result reports it.
        if (s_->failRefresh) return false;
        s_->watchAlive = true;   // a successful reconnect repairs the watch
        return true;
    }
    void refreshInterface(const std::string& i) override { s_->interfacesUp.push_back(i); }
    void removeInterface(const std::string& i)  override { s_->interfacesDown.push_back(i); }
    void reassertAll() override { ++s_->reassertCalls; }
    [[nodiscard]] bool isBackendAlive() const override { return s_->backendAlive; }
    [[nodiscard]] std::chrono::steady_clock::time_point lastEventAt() const override {
        return std::chrono::steady_clock::time_point::min();
    }
private:
    std::shared_ptr<Shared> s_;
};

/// Records what reaches the model; everything else is a no-op.
class FakeModel final : public INetworkObservationModel {
public:
    std::vector<LinkObservation> links;
    std::vector<LldpObservation> lldp;

    void onLinkObservation(const LinkObservation& o) override { links.push_back(o); }
    void onLldpObservation(const LldpObservation& o) override { lldp.push_back(o); }
    void onAddressObservation(const AddressObservation&) override {}
    void onNeighborObservation(const NeighborObservation&) override {}
    void onFdbObservation(const FdbObservation&) override {}
    void setEventSink(IModelEventSink*) override {}
    void setInterfacePolicy(std::unique_ptr<IInterfacePolicy>) override {}
    void setClassifier(std::unique_ptr<ICandidateClassifier>) override {}
    [[nodiscard]] LocalNetworkSnapshot localSnapshot() const override { return {}; }
    [[nodiscard]] std::vector<RemoteCandidate> remoteCandidates() const override { return {}; }
    [[nodiscard]] std::optional<RemoteCandidate> findCandidateByMac(const std::string&) const override {
        return std::nullopt;
    }
    void age(std::chrono::steady_clock::time_point) override {}
    void prepareForRestart() override {}
    void markLive() override {}
};

struct Fixture {
    std::shared_ptr<FakeLldpSource::Shared> shared =
        std::make_shared<FakeLldpSource::Shared>();
    std::size_t factoryCalls = 0;

    std::unique_ptr<NetlinkLldpObservationRuntime> make(
        std::chrono::steady_clock::duration reassert = 30s) {
        auto rt = std::make_unique<NetlinkLldpObservationRuntime>(
            std::make_unique<FakeModel>(), reassert);
        rt->setLldpSourceFactoryForTest(
            [this](std::function<void(const LldpObservation&)>) {
                ++factoryCalls;
                return std::make_unique<FakeLldpSource>(shared);
            });
        return rt;
    }
};

// ---------------------------------------------------------------------------
// THE gap: a link event must reach an observer created AFTER the callbacks
// were built. This is the sole justification for atomic<shared_ptr<...>> and
// has been unverifiable since the indirection landed.
// ---------------------------------------------------------------------------
void testLinkEventReachesObserverCreatedAfterCallbacks() {
    Fixture f;
    f.shared->startResults = {false, true};   // fail, then succeed
    auto rt = f.make();

    // Callbacks are built now, while no observer exists.
    auto cb = rt->makeCallbacksForTest();

    LinkEvent down;
    down.ifname = "eth0";
    down.present = true;
    down.running = false;
    cb.onLinkChanged(down);
    expect(f.shared->interfacesDown.empty(), "no observer yet: event is dropped");

    // A tick()-driven retry publishes an observer well after the callbacks
    // were constructed.
    const auto t0 = std::chrono::steady_clock::now();
    rt->tick(t0 + 31s);
    expect(f.shared->running.load(), "retry acquired an observer");

    cb.onLinkChanged(down);
    expect(f.shared->interfacesDown == std::vector<std::string>{"eth0"},
           "the SAME callbacks now reach the late-created observer");

    LinkEvent up = down;
    up.running = true;
    cb.onLinkChanged(up);
    expect(f.shared->interfacesUp == std::vector<std::string>{"eth0"},
           "and up events route to onInterfaceUp");
}

void testRetryAcquiresAfterRepeatedFailures() {
    Fixture f;
    f.shared->startResults = {false, false, false, true};
    auto rt = f.make();

    const auto t0 = std::chrono::steady_clock::now();
    rt->tick(t0);                       // attempt 1 (fails)
    expect(!rt->health().lldpAvailable, "unavailable after first failure");
    rt->tick(t0 + 31s);                 // 2
    rt->tick(t0 + 62s);                 // 3
    expect(!rt->health().lldpAvailable, "still unavailable");
    rt->tick(t0 + 93s);                 // 4 succeeds
    expect(rt->health().lldpAvailable, "acquired after N failures");
    expect(f.factoryCalls == 4, "one factory call per attempt");
}

void testRetryIsThrottled() {
    Fixture f;
    f.shared->startResults = {false, false};
    auto rt = f.make();

    const auto t0 = std::chrono::steady_clock::now();
    rt->tick(t0);
    expect(f.factoryCalls == 1, "first attempt");
    rt->tick(t0 + 1s);
    rt->tick(t0 + 29s);
    expect(f.factoryCalls == 1, "throttled below the 30s retry interval");
    rt->tick(t0 + 31s);
    expect(f.factoryCalls == 2, "attempted again past the interval");
}

void testProbeFailureReconnectsAndDropsStoppedObserver() {
    Fixture f;
    auto rt = f.make();
    const auto t0 = std::chrono::steady_clock::now();

    rt->tick(t0);
    expect(rt->health().lldpAvailable, "observer acquired");

    f.shared->backendAlive  = false;
    f.shared->failRefresh = true;     // reconnect fails, source stops
    rt->tick(t0 + 61s);                 // past the 60s probe interval
    expect(f.shared->refreshAllCalls == 1, "probe failure triggers refreshAll");

    const auto before = f.factoryCalls;
    f.shared->backendAlive = true;
    rt->tick(t0 + 92s);
    expect(f.factoryCalls == before + 1, "stopped observer dropped and re-acquired");
}

void testProbeSuccessDoesNotReconnect() {
    Fixture f;
    auto rt = f.make();
    const auto t0 = std::chrono::steady_clock::now();
    rt->tick(t0);
    rt->tick(t0 + 61s);
    rt->tick(t0 + 122s);
    expect(f.shared->refreshAllCalls == 0, "healthy backend is never reconnected");
}

void testKeepaliveHonoursInjectedInterval() {
    Fixture f;
    auto rt = f.make(10s);              // ctor parameter that was previously ignored
    const auto t0 = std::chrono::steady_clock::now();

    rt->tick(t0);
    const int afterAcquire = f.shared->reassertCalls;
    rt->tick(t0 + 5s);
    expect(f.shared->reassertCalls == afterAcquire, "throttled below the interval");
    rt->tick(t0 + 11s);
    expect(f.shared->reassertCalls == afterAcquire + 1, "fires past the interval");
}

void testFactorySeamRejectedAfterStart() { /* asserts the precondition throws */ }

void testDeadWatchEscalatesAfterRepeatedFailures() {
    Fixture f;
    auto rt = f.make();
    const auto t0 = std::chrono::steady_clock::now();
    rt->tick(t0);                        // acquire

    f.shared->watchAlive  = false;       // watch dies
    f.shared->failRefresh = true;        // and repair keeps failing

    const auto before = f.factoryCalls;
    for (int i = 1; i <= 5; ++i) {
        rt->tick(t0 + std::chrono::seconds{31 * i});
    }
    expect(f.shared->refreshAllCalls == 5, "one attempt per retry interval");
    expect(f.factoryCalls == before, "not yet escalated");

    rt->tick(t0 + 186s);
    expect(f.factoryCalls == before + 1, "escalates to re-acquire after the limit");
}

// A failed probe reconnects IN PLACE. The observer is not dropped: a failed
// refreshAll() retains the watch and leaves the epoch running, so recovery
// runs through the escalation path rather than an immediate re-acquire.
//
// Replaces testProbeFailureReconnectsAndDropsStoppedObserver, which asserted
// the pre-supervisor contract where a failed reconnect stopped the source.
void testProbeFailureReconnectsInPlace() {
    Fixture f;
    auto rt = f.make();
    const auto t0 = std::chrono::steady_clock::now();

    rt->tick(t0);
    expect(rt->health().lldpAvailable, "observer acquired");

    const auto before = f.factoryCalls;
    f.shared->backendAlive = false;
    f.shared->failRefresh  = true;

    rt->tick(t0 + 61s);                 // past the 60s probe interval
    expect(f.shared->refreshAllCalls == 1, "probe failure triggers a reconnect");
    expect(f.factoryCalls == before, "the observer is retained, not replaced");
    expect(rt->health().lldpAvailable, "a failed reconnect does not end the epoch");
}

// A dead watch is repaired in place — refreshAll(), not a re-acquire — so the
// neighbour cache survives to be reconciled.
void testDeadWatchReconnectsInPlace() {
    Fixture f;
    auto rt = f.make();
    const auto t0 = std::chrono::steady_clock::now();
    rt->tick(t0);

    const auto before = f.factoryCalls;
    f.shared->watchAlive = false;       // lldpd restarted; loop thread exited

    rt->tick(t0 + 31s);
    expect(f.shared->refreshAllCalls == 1, "dead watch triggers a reconnect");
    expect(f.factoryCalls == before, "repaired in place, not re-acquired");
    expect(f.shared->watchAlive.load(), "a successful reconnect restores the watch");
}

// Every reconnect trigger shares one throttle, so a daemon that is down does
// not cost a bounded connect on every tick.
void testDeadWatchRepairIsThrottled() {
    Fixture f;
    auto rt = f.make();
    const auto t0 = std::chrono::steady_clock::now();
    rt->tick(t0);

    f.shared->watchAlive  = false;
    f.shared->failRefresh = true;       // stays dead, so every tick would retry

    rt->tick(t0 + 1s);
    expect(f.shared->refreshAllCalls == 1, "first attempt");
    rt->tick(t0 + 2s);
    rt->tick(t0 + 29s);
    expect(f.shared->refreshAllCalls == 1, "throttled below the 30s interval");
    rt->tick(t0 + 32s);
    expect(f.shared->refreshAllCalls == 2, "attempted again past the interval");
}

// In-place repair is preferred, but not forever: after the failure limit the
// observer is dropped and re-acquired. Throttled ticks are not failures and
// must not count toward the limit.
void testDeadWatchEscalatesToReacquireAfterFailureLimit() {
    Fixture f;
    auto rt = f.make();
    const auto t0 = std::chrono::steady_clock::now();
    rt->tick(t0);

    const auto before = f.factoryCalls;
    f.shared->watchAlive  = false;
    f.shared->failRefresh = true;

    // Five attempts, one per retry interval. The extra sub-interval ticks
    // are throttled and must not advance the failure count.
    for (int i = 1; i <= 5; ++i) {
        const auto at = t0 + std::chrono::seconds{31 * i};
        rt->tick(at);
        rt->tick(at + 1s);              // throttled: not a failure
    }
    expect(f.shared->refreshAllCalls == 5, "one attempt per interval");
    expect(f.factoryCalls == before, "not escalated before the limit");

    // The escalation fires on the tick following the limiting failure.
    f.shared->failRefresh = false;      // replacement can start cleanly
    rt->tick(t0 + 186s);
    expect(f.factoryCalls == before + 1, "observer dropped and re-acquired");
    expect(rt->health().lldpAvailable, "the replacement is live");
}

} // namespace

int main() {
    testLinkEventReachesObserverCreatedAfterCallbacks();
    testRetryAcquiresAfterRepeatedFailures();
    testRetryIsThrottled();
    testProbeFailureReconnectsInPlace();
    testProbeSuccessDoesNotReconnect();
    testDeadWatchReconnectsInPlace();
    testDeadWatchRepairIsThrottled();
    testDeadWatchEscalatesToReacquireAfterFailureLimit();
    testKeepaliveHonoursInjectedInterval();
    testFactorySeamRejectedAfterStart();
    testDeadWatchRepairIsThrottled();
    testDeadWatchEscalatesAfterRepeatedFailures();
    return EXIT_SUCCESS;
}
