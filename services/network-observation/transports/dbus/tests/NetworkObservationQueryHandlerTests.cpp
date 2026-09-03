//
// Created by vvass on 03-Sep-26.
//

#include "NetworkObservationQueryHandler.h"

#include <IObservationQueryService.h>
#include <ServiceBinding.h>
#include <IngressLimits.hpp>
#include <network_observation/NetworkObservationContracts.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using namespace RSCGroup;

namespace contract = interop_contract::network_observation;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

template<typename Function>
void expectContained(Function&& function, const std::string& message)
{
    try {
        std::forward<Function>(function)();
    } catch (const std::exception& error) {
        std::cerr << message
                  << ": escaped std::exception: "
                  << error.what() << '\n';
        std::exit(EXIT_FAILURE);
    } catch (...) {
        std::cerr << message
                  << ": escaped non-standard exception\n";
        std::exit(EXIT_FAILURE);
    }
}

template<typename Predicate>
bool waitFor(
    Predicate predicate,
    std::chrono::milliseconds timeout = 500ms)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }

    return predicate();
}

class FakeObservationQueryService final : public IObservationQueryService {
public:
    enum class Failure {
        none,
        standard,
        nonStandard,
    };

    LocalNetworkSnapshot localSnapshot() const override
    {
        ++localSnapshotCalls_;
        maybeThrow();

        std::unique_lock lock(snapshotMutex_);

        if (blockSnapshot_) {
            snapshotEntered_ = true;
            snapshotEnteredCv_.notify_all();

            snapshotReleaseCv_.wait(lock, [this] {
                return snapshotReleased_;
            });
        }

        return snapshot_;
    }

    std::optional<LocalInterfaceState>
    getInterface(const std::string& ifname) const override
    {
        ++interfaceCalls_;
        maybeThrow();

        if (interface_ && interface_->ifname == ifname) {
            return interface_;
        }

        return std::nullopt;
    }

    std::vector<RemoteCandidate> remoteCandidates() const override
    {
        ++remoteCandidateCalls_;
        maybeThrow();
        return candidates_;
    }

    std::optional<RemoteCandidate>
    getCandidateByMac(const std::string& mac) const override
    {
        ++candidateByMacCalls_;
        maybeThrow();

        for (const auto& candidate : candidates_) {
            if (candidate.mac == mac) {
                return candidate;
            }
        }

        return std::nullopt;
    }

    contract::ObservationIssues getIssues() const override
    {
        ++issueCalls_;
        maybeThrow();
        return issues_;
    }

    bool isReady() const override
    {
        ++readyCalls_;
        maybeThrow();
        return ready_;
    }

    std::string getPhase() const override
    {
        ++phaseCalls_;
        maybeThrow();
        return phase_;
    }

    void setFailure(Failure failure) noexcept
    {
        failure_.store(failure, std::memory_order_release);
    }

    void setReady(bool ready) noexcept
    {
        ready_.store(ready, std::memory_order_release);
    }

    void setPhase(std::string phase)
    {
        phase_ = std::move(phase);
    }

    void setInterface(LocalInterfaceState interface)
    {
        interface_ = std::move(interface);
        snapshot_.interfaces[interface_->ifname] = *interface_;
    }

    void setCandidate(RemoteCandidate candidate)
    {
        candidates_.clear();
        candidates_.push_back(std::move(candidate));
    }

    void setCandidates(std::vector<RemoteCandidate> candidates)
    {
        candidates_ = std::move(candidates);
    }

    void setSnapshot(LocalNetworkSnapshot snapshot)
    {
        snapshot_ = std::move(snapshot);
    }


    void setBlockSnapshot(bool value)
    {
        std::scoped_lock lock(snapshotMutex_);

        blockSnapshot_ = value;
        snapshotEntered_ = false;
        snapshotReleased_ = !value;
    }

    [[nodiscard]] bool waitUntilSnapshotEntered(std::chrono::milliseconds timeout = 500ms) const
    {
        std::unique_lock lock(snapshotMutex_);
        return snapshotEnteredCv_.wait_for(lock, timeout, [this] { return snapshotEntered_; });
    }

    void releaseSnapshot() const
    {
        {
            std::scoped_lock lock(snapshotMutex_);
            snapshotReleased_ = true;
        }

        snapshotReleaseCv_.notify_all();
    }

private:
    void maybeThrow() const
    {
        switch (failure_.load(std::memory_order_acquire)) {
            case Failure::none:
                return;

            case Failure::standard:
                throw std::runtime_error(
                    "fake observation query failure");

            case Failure::nonStandard:
                throw 42;
        }
    }

    mutable std::atomic<Failure> failure_{Failure::none};

    mutable std::atomic<int> localSnapshotCalls_{0};
    mutable std::atomic<int> interfaceCalls_{0};
    mutable std::atomic<int> remoteCandidateCalls_{0};
    mutable std::atomic<int> candidateByMacCalls_{0};
    mutable std::atomic<int> issueCalls_{0};
    mutable std::atomic<int> readyCalls_{0};
    mutable std::atomic<int> phaseCalls_{0};

    LocalNetworkSnapshot snapshot_;
    std::optional<LocalInterfaceState> interface_;
    std::vector<RemoteCandidate> candidates_;
    contract::ObservationIssues issues_;

    std::atomic<bool> ready_{true};
    std::string phase_{std::string(contract::PHASE_LIVE)};

    mutable std::mutex snapshotMutex_;
    mutable std::condition_variable snapshotEnteredCv_;
    mutable std::condition_variable snapshotReleaseCv_;
    mutable bool blockSnapshot_{false};
    mutable bool snapshotEntered_{false};
    mutable bool snapshotReleased_{true};
};

void expectFallbacks(
    NetworkObservationQueryHandler& handler,
    const std::string& context)
{
    expect(
        handler.getLocalSnapshot().empty(),
        context + ": local-snapshot fallback must be empty");

    expect(
        handler.getInterface("eth0").empty(),
        context + ": interface fallback must be empty");

    expect(
        handler.getRemoteCandidateMacs().empty(),
        context + ": candidate-MAC fallback must be empty");

    expect(
        handler.getCandidateByMac("00:11:22:33:44:55").empty(),
        context + ": candidate fallback must be empty");

    expect(
        handler.getIssues().empty(),
        context + ": issues fallback must be empty");

    expect(
        !handler.getReady(),
        context + ": ready fallback must be false");

    expect(
        handler.getPhase() == contract::PHASE_STOPPED,
        context + ": phase fallback must be stopped");
}

void testFallbacksBeforeBind()
{
    ServiceBinding<IObservationQueryService> binding;
    NetworkObservationQueryHandler handler(binding);

    expectFallbacks(handler, "before bind");
}

void testBoundQueriesReachService()
{
    ServiceBinding<IObservationQueryService> binding;
    FakeObservationQueryService service;
    NetworkObservationQueryHandler handler(binding);

    LocalInterfaceState interface;
    interface.ifindex = 7;
    interface.ifname = "eth0";
    interface.mac = "aa:bb:cc:dd:ee:ff";
    interface.adminUp = true;
    interface.running = true;
    interface.operstate = "UP";
    service.setInterface(interface);

    RemoteCandidate candidate;
    candidate.mac = "00:11:22:33:44:55";
    candidate.classification =
        CandidateClassification::RemoteEndpoint;
    candidate.status = CandidateStatus::Confirmed;
    service.setCandidate(candidate);

    service.setReady(true);
    service.setPhase(std::string(contract::PHASE_LIVE));

    binding.bind(&service);

    const auto snapshot = handler.getLocalSnapshot();
    expect(
        snapshot.contains("eth0"),
        "bound snapshot query should encode the service snapshot");

    expect(
        !handler.getInterface("eth0").empty(),
        "bound interface query should encode the service result");

    const auto macs = handler.getRemoteCandidateMacs();
    expect(
        macs.size() == 1 &&
            macs.front() == "00:11:22:33:44:55",
        "bound candidate-MAC query should reach the service");

    expect(
        !handler.getCandidateByMac(
            "00:11:22:33:44:55").empty(),
        "bound candidate query should encode the service result");

    expect(
        handler.getReady(),
        "bound readiness query should reach the service");

    expect(
        handler.getPhase() == contract::PHASE_LIVE,
        "bound phase query should reach the service");

    binding.detach();
}

void testFallbacksAfterDetach()
{
    ServiceBinding<IObservationQueryService> binding;
    FakeObservationQueryService service;
    NetworkObservationQueryHandler handler(binding);

    binding.bind(&service);

    expect(
        handler.getReady(),
        "bound handler should reach the service before detach");

    binding.detach();

    expectFallbacks(handler, "after detach");
}

void testStandardExceptionsAreContained()
{
    ServiceBinding<IObservationQueryService> binding;
    FakeObservationQueryService service;
    NetworkObservationQueryHandler handler(binding);

    binding.bind(&service);
    service.setFailure(
        FakeObservationQueryService::Failure::standard);

    expectContained(
        [&] {
            expectFallbacks(handler, "standard exception");
        },
        "standard query exceptions must be contained");

    binding.detach();
}

void testNonStandardExceptionsAreContained()
{
    ServiceBinding<IObservationQueryService> binding;
    FakeObservationQueryService service;
    NetworkObservationQueryHandler handler(binding);

    binding.bind(&service);
    service.setFailure(
        FakeObservationQueryService::Failure::nonStandard);

    expectContained(
        [&] {
            expectFallbacks(handler, "non-standard exception");
        },
        "non-standard query exceptions must be contained");

    binding.detach();
}
void testEncodingExceptionsAreContained()
{
    ServiceBinding<IObservationQueryService> binding;
    FakeObservationQueryService service;
    NetworkObservationQueryHandler handler(binding);

    LocalInterfaceState interface;
    interface.ifname = std::string(interop_contract::ingress::kMaxStringLength + 1, 'x');
    interface.mac = "aa:bb:cc:dd:ee:ff";
    interface.operstate = "UP";
    service.setInterface(interface);
    binding.bind(&service);

    expectContained([&] { expect(handler.getInterface(interface.ifname).empty(), "encoding failure should return the interface fallback"); }, "interface encoding exception must be contained");

    binding.detach();
}

void testDetachWaitsForAdmittedQueryAndRejectsNewQueries()
{
    ServiceBinding<IObservationQueryService> binding;
    FakeObservationQueryService service;
    NetworkObservationQueryHandler handler(binding);

    service.setBlockSnapshot(true);
    binding.bind(&service);

    auto activeQuery = std::async(std::launch::async, [&handler] { return handler.getLocalSnapshot(); });
    const bool queryEntered = service.waitUntilSnapshotEntered();

    if (!queryEntered) {
        service.releaseSnapshot();
        activeQuery.wait();
        expect(false, "snapshot query did not enter before timeout");
    }

    auto detachFuture = std::async(std::launch::async, [&binding] { binding.detach(); });

    const bool admissionClosed = waitFor([&] { return handler.getPhase() == contract::PHASE_STOPPED; });
    const bool detachWaited = detachFuture.wait_for(25ms) == std::future_status::timeout;
    const bool queryRemainedBlocked = activeQuery.wait_for(25ms) == std::future_status::timeout;

    service.releaseSnapshot();

    activeQuery.get();
    const bool detachCompleted = detachFuture.wait_for(500ms) == std::future_status::ready;

    if (detachCompleted) {
        detachFuture.get();
    }

    expect(admissionClosed, "detach must close admission before waiting for active queries");
    expect(detachWaited, "detach must wait while an admitted query remains active");
    expect(queryRemainedBlocked, "the admitted snapshot query should remain blocked");
    expect(detachCompleted, "detach should finish after the active query releases its lease");
    expectFallbacks(handler, "after completed detach");
}

void testBindAfterDetachReopensAdmission()
{
    ServiceBinding<IObservationQueryService> binding;
    FakeObservationQueryService first;
    FakeObservationQueryService second;
    NetworkObservationQueryHandler handler(binding);

    first.setReady(false);
    first.setPhase(std::string(contract::PHASE_INITIALIZING));

    second.setReady(true);
    second.setPhase(std::string(contract::PHASE_LIVE));

    binding.bind(&first);

    expect(
        !handler.getReady(),
        "handler should initially reach the first service");

    expect(
        handler.getPhase() == contract::PHASE_INITIALIZING,
        "handler should expose the first service phase");

    binding.detach();

    expect(
        handler.getPhase() == contract::PHASE_STOPPED,
        "detached handler should return the stopped fallback");

    binding.bind(&second);

    expect(
        handler.getReady(),
        "rebinding should reopen admission");

    expect(
        handler.getPhase() == contract::PHASE_LIVE,
        "handler should reach the newly bound service");

    binding.detach();
}

void testOversizedSnapshotReturnsFallback()
{
    ServiceBinding<IObservationQueryService> binding;
    FakeObservationQueryService service;
    NetworkObservationQueryHandler handler(binding);

    LocalNetworkSnapshot snapshot;
    for (std::size_t index = 0; index <= interop_contract::ingress::network_observation::kMaxInterfaces; ++index) {
        LocalInterfaceState interface;
        interface.ifindex = static_cast<int>(index);
        interface.ifname = "eth" + std::to_string(index);
        interface.mac = "aa:bb:cc:dd:ee:ff";
        interface.operstate = "UP";
        snapshot.interfaces.emplace(interface.ifname, std::move(interface));
    }

    service.setSnapshot(std::move(snapshot));
    binding.bind(&service);

    expect(handler.getLocalSnapshot().empty(), "oversized snapshot should return the handler fallback");

    binding.detach();
}

void testOversizedCandidateListReturnsFallback()
{
    ServiceBinding<IObservationQueryService> binding;
    FakeObservationQueryService service;
    NetworkObservationQueryHandler handler(binding);

    std::vector<RemoteCandidate> candidates;
    candidates.reserve(interop_contract::ingress::network_observation::kMaxCandidates + 1);

    for (std::size_t index = 0; index <= interop_contract::ingress::network_observation::kMaxCandidates; ++index) {
        RemoteCandidate candidate;
        candidate.mac = "00:11:22:33:44:55";
        candidates.push_back(std::move(candidate));
    }

    service.setCandidates(std::move(candidates));
    binding.bind(&service);

    expect(handler.getRemoteCandidateMacs().empty(), "oversized candidate list should return the handler fallback");

    binding.detach();
}

} // namespace

int main()
{
    testFallbacksBeforeBind();
    testBoundQueriesReachService();
    testFallbacksAfterDetach();
    testStandardExceptionsAreContained();
    testNonStandardExceptionsAreContained();
    testEncodingExceptionsAreContained();
    testDetachWaitsForAdmittedQueryAndRejectsNewQueries();
    testOversizedSnapshotReturnsFallback();
    testOversizedCandidateListReturnsFallback();
    testBindAfterDetachReopensAdmission();

    return EXIT_SUCCESS;
}