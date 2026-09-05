#include "LifecycleCoordinator.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

namespace {

using namespace RSCGroup;
using State = LifecycleCoordinator::State;
using WaitPolicy = LifecycleCoordinator::WaitPolicy;

constexpr auto shortTimeout = std::chrono::milliseconds(100);
constexpr auto testTimeout = std::chrono::seconds(5);

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr
            << "LifecycleCoordinatorTests: "
            << message
            << '\n';

        std::exit(EXIT_FAILURE);
    }
}

class Gate {
public:
    void open()
    {
        {
            std::scoped_lock lock(mutex_);
            open_ = true;
        }

        cv_.notify_all();
    }

    void wait()
    {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] {
            return open_;
        });
    }

    [[nodiscard]] bool waitFor(
        std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(mutex_);

        return cv_.wait_for(lock, timeout, [this] {
            return open_;
        });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool open_{false};
};

void testStartFromStoppedClaimsStarting()
{
    LifecycleCoordinator coordinator;

    expect(
        coordinator.state() == State::stopped,
        "initial state should be stopped");

    auto transition = coordinator.beginStart();

    expect(
        transition.owned(),
        "beginStart() in stopped must claim the transition");

    expect(
        coordinator.state() == State::starting,
        "state should be starting while claimed");

    transition.complete();

    expect(
        coordinator.state() == State::running,
        "completed start should reach running");

    expect(
        coordinator.isRunning(),
        "isRunning() should be true");
}

void testStartWhileRunningReturnsUnowned()
{
    LifecycleCoordinator coordinator;

    auto start = coordinator.beginStart();
    start.complete();

    auto second = coordinator.beginStart();

    expect(
        !second.owned(),
        "beginStart() while running must return an unowned transition");

    expect(
        coordinator.state() == State::running,
        "an unowned transition must not change state");
}

void testFailedStartReturnsToStopped()
{
    LifecycleCoordinator coordinator;

    auto transition = coordinator.beginStart();
    transition.fail();

    expect(
        coordinator.state() == State::stopped,
        "failed start must resolve to stopped");
}

void testAbandonedStartResolvesToStopped()
{
    LifecycleCoordinator coordinator;

    {
        auto transition = coordinator.beginStart();

        expect(
            transition.owned(),
            "beginStart() should claim");
    }

    expect(
        coordinator.state() == State::stopped,
        "abandoned start must resolve to stopped");
}

void testAbandonedStopResolvesToStopped()
{
    LifecycleCoordinator coordinator;

    auto start = coordinator.beginStart();
    start.complete();

    {
        auto stop = coordinator.beginStop();

        expect(
            stop.owned(),
            "beginStop() in running should claim");
    }

    expect(
        coordinator.state() == State::stopped,
        "abandoned stop must resolve to stopped");
}

void testFailedStopResolvesToStopped()
{
    LifecycleCoordinator coordinator;

    auto start = coordinator.beginStart();
    start.complete();

    auto stop = coordinator.beginStop();
    stop.fail();

    expect(
        coordinator.state() == State::stopped,
        "failed stop must still resolve to stopped");
}

void testStopWhenStoppedReturnsUnowned()
{
    LifecycleCoordinator coordinator;

    auto stop = coordinator.beginStop();

    expect(
        !stop.owned(),
        "beginStop() in stopped must return an unowned transition");

    expect(
        coordinator.state() == State::stopped,
        "state must remain stopped");
}

void testConcurrentStopWaitsForTeardownCompletion()
{
    LifecycleCoordinator coordinator;

    auto start = coordinator.beginStart();
    start.complete();

    Gate teardownEntered;
    Gate releaseTeardown;

    std::atomic<bool> teardownFinished{false};
    std::atomic<bool> secondSawCompletion{false};

    auto first = std::async(std::launch::async, [&] {
        auto stop = coordinator.beginStop();

        expect(
            stop.owned(),
            "the first stop must own teardown");

        teardownEntered.open();
        releaseTeardown.wait();

        teardownFinished.store(
            true,
            std::memory_order_release);

        stop.complete();
    });

    teardownEntered.wait();

    auto second = std::async(std::launch::async, [&] {
        auto stop = coordinator.beginStop();

        expect(
            !stop.owned(),
            "the second stop must not own teardown");

        secondSawCompletion.store(
            teardownFinished.load(
                std::memory_order_acquire),
            std::memory_order_release);
    });

    expect(
        second.wait_for(shortTimeout) ==
            std::future_status::timeout,
        "the second stop must wait while teardown is active");

    releaseTeardown.open();

    first.get();
    second.get();

    expect(
        secondSawCompletion.load(
            std::memory_order_acquire),
        "the second stop must return after teardown completed");

    expect(
        coordinator.state() == State::stopped,
        "state should be stopped");
}

void testStopDuringOrdinaryStartupWaitsAndClaims()
{
    LifecycleCoordinator coordinator;

    Gate startupEntered;
    Gate releaseStartup;

    auto starter = std::async(std::launch::async, [&] {
        auto start = coordinator.beginStart();

        expect(
            start.owned(),
            "beginStart() should claim");

        startupEntered.open();
        releaseStartup.wait();
        start.complete();
    });

    startupEntered.wait();

    std::atomic<bool> claimedStop{false};

    auto stopper = std::async(std::launch::async, [&] {
        auto stop = coordinator.beginStop();

        claimedStop.store(
            stop.owned(),
            std::memory_order_release);

        stop.complete();
    });

    expect(
        stopper.wait_for(shortTimeout) ==
            std::future_status::timeout,
        "stop during ordinary startup must wait");

    releaseStartup.open();

    starter.get();
    stopper.get();

    expect(
        claimedStop.load(std::memory_order_acquire),
        "stop must claim teardown after startup succeeds");

    expect(
        coordinator.state() == State::stopped,
        "state should be stopped");
}

void testStopDuringFailedOrdinaryStartupDoesNotClaim()
{
    LifecycleCoordinator coordinator;

    Gate startupEntered;
    Gate releaseStartup;

    auto starter = std::async(std::launch::async, [&] {
        auto start = coordinator.beginStart();

        startupEntered.open();
        releaseStartup.wait();
        start.fail();
    });

    startupEntered.wait();

    std::atomic<bool> claimedStop{true};

    auto stopper = std::async(std::launch::async, [&] {
        auto stop = coordinator.beginStop();

        claimedStop.store(
            stop.owned(),
            std::memory_order_release);
    });

    releaseStartup.open();

    starter.get();
    stopper.get();

    expect(
        !claimedStop.load(std::memory_order_acquire),
        "stop must not claim teardown after failed startup");

    expect(
        coordinator.state() == State::stopped,
        "state should be stopped");
}

void testStartDuringStoppingWaitsAndClaimsNewEpoch()
{
    LifecycleCoordinator coordinator;

    auto start = coordinator.beginStart();
    start.complete();

    Gate teardownEntered;
    Gate releaseTeardown;

    auto stopper = std::async(std::launch::async, [&] {
        auto stop = coordinator.beginStop();

        teardownEntered.open();
        releaseTeardown.wait();
        stop.complete();
    });

    teardownEntered.wait();

    std::atomic<bool> claimedStart{false};

    auto starter = std::async(std::launch::async, [&] {
        auto restart = coordinator.beginStart();

        claimedStart.store(
            restart.owned(),
            std::memory_order_release);

        restart.complete();
    });

    expect(
        starter.wait_for(shortTimeout) ==
            std::future_status::timeout,
        "start during stopping must wait until stopped");

    releaseTeardown.open();

    stopper.get();
    starter.get();

    expect(
        claimedStart.load(std::memory_order_acquire),
        "start must claim a new epoch after teardown");

    expect(
        coordinator.state() == State::running,
        "state should be running again");
}

void testCancellableStartCompletesNormally()
{
    LifecycleCoordinator coordinator;

    auto start = coordinator.beginCancellableStart();

    expect(
        start.owned(),
        "cancellable start should claim from stopped");

    expect(
        coordinator.state() == State::starting,
        "cancellable start should publish starting");

    expect(
        start.stopToken().stop_possible(),
        "cancellable start should provide a stoppable token");

    expect(
        !start.stopRequested(),
        "new cancellable start should not be cancelled");

    expect(
        coordinator.isStartOwnerThread(),
        "claiming thread should be the start owner");

    expect(
        start.tryComplete(),
        "uncancelled startup should complete");

    expect(
        coordinator.state() == State::running,
        "completed cancellable start should reach running");

    expect(
        !coordinator.isStartOwnerThread(),
        "completed startup should clear owner-thread state");
}

void testCancellableStartWhileRunningReturnsUnowned()
{
    LifecycleCoordinator coordinator;

    auto start = coordinator.beginStart();
    start.complete();

    auto second = coordinator.beginCancellableStart();

    expect(
        !second.owned(),
        "cancellable start while running should be unowned");

    expect(
        !second.stopToken().stop_possible(),
        "unowned cancellable start should have no stoppable token");

    auto stop = coordinator.beginStop();
    stop.complete();
}

void testAbandonedCancellableStartResolvesToStopped()
{
    LifecycleCoordinator coordinator;

    {
        auto start = coordinator.beginCancellableStart();

        expect(
            start.owned(),
            "cancellable start should claim from stopped");
    }

    expect(
        coordinator.state() == State::stopped,
        "abandoned cancellable start should resolve to stopped");

    expect(
        !coordinator.isStartOwnerThread(),
        "abandonment should clear start-owner state");
}

void testStopCancelsStartupAndWaitsForRollback()
{
    LifecycleCoordinator coordinator;

    Gate startupEntered;
    Gate cancellationObserved;
    Gate completionAttempted;
    Gate releaseRollback;

    std::atomic<bool> completionAccepted{true};

    auto starter = std::async(std::launch::async, [&] {
        auto start = coordinator.beginCancellableStart();

        expect(
            start.owned(),
            "cancellable startup should claim transition");

        std::stop_callback cancellationCallback(
            start.stopToken(),
            [&] {
                cancellationObserved.open();
            });

        startupEntered.open();
        cancellationObserved.wait();

        completionAccepted.store(
            start.tryComplete(),
            std::memory_order_release);

        completionAttempted.open();
        releaseRollback.wait();

        start.fail();
    });

    startupEntered.wait();

    std::atomic<bool> stopOwned{true};

    auto stopper = std::async(std::launch::async, [&] {
        auto stop = coordinator.beginStop();

        stopOwned.store(
            stop.owned(),
            std::memory_order_release);
    });

    expect(
        cancellationObserved.waitFor(testTimeout),
        "stop should request the startup stop token");

    expect(
        completionAttempted.waitFor(testTimeout),
        "startup should observe cancellation");

    expect(
        !completionAccepted.load(std::memory_order_acquire),
        "tryComplete() must reject cancelled startup");

    expect(
        coordinator.state() == State::starting,
        "state must remain starting until rollback completes");

    expect(
        stopper.wait_for(shortTimeout) ==
            std::future_status::timeout,
        "waiting stop must wait for startup rollback");

    releaseRollback.open();

    starter.get();
    stopper.get();

    expect(
        !stopOwned.load(std::memory_order_acquire),
        "stop should be unowned after cancelled startup reaches stopped");

    expect(
        coordinator.state() == State::stopped,
        "cancelled startup should resolve to stopped");
}

void testNoWaitStopRequestsCancellationImmediately()
{
    LifecycleCoordinator coordinator;

    auto start = coordinator.beginCancellableStart();

    std::atomic<int> cancellationCallbacks{0};

    std::stop_callback cancellationCallback(
        start.stopToken(),
        [&] {
            cancellationCallbacks.fetch_add(
                1,
                std::memory_order_relaxed);
        });

    auto stop = coordinator.beginStop(
        WaitPolicy::no_wait);

    expect(
        !stop.owned(),
        "no-wait stop during startup should be unowned");

    expect(
        start.stopRequested(),
        "no-wait stop should request startup cancellation");

    expect(
        cancellationCallbacks.load(
            std::memory_order_relaxed) == 1,
        "startup cancellation callback should run once");

    expect(
        coordinator.state() == State::starting,
        "no-wait stop must leave state starting during rollback");

    expect(
        !start.tryComplete(),
        "cancelled startup must not publish running");

    start.fail();

    expect(
        coordinator.state() == State::stopped,
        "rollback should resolve cancelled startup to stopped");
}

void testStopCallbackMayReenterCoordinator()
{
    LifecycleCoordinator coordinator;

    auto start = coordinator.beginCancellableStart();

    std::atomic<bool> callbackReturned{false};
    std::atomic<bool> nestedOwned{true};

    std::stop_callback cancellationCallback(
        start.stopToken(),
        [&] {
            auto nestedStop = coordinator.beginStop(
                WaitPolicy::no_wait);

            nestedOwned.store(
                nestedStop.owned(),
                std::memory_order_release);

            callbackReturned.store(
                true,
                std::memory_order_release);
        });

    auto outerStop = coordinator.beginStop(
        WaitPolicy::no_wait);

    expect(
        !outerStop.owned(),
        "outer no-wait stop should be unowned");

    expect(
        callbackReturned.load(std::memory_order_acquire),
        "cancellation callback should return without deadlock");

    expect(
        !nestedOwned.load(std::memory_order_acquire),
        "re-entrant no-wait stop should be unowned");

    start.fail();

    expect(
        coordinator.state() == State::stopped,
        "startup rollback should still complete normally");
}

void testCancellationWinsBeforeTryComplete()
{
    LifecycleCoordinator coordinator;

    auto start = coordinator.beginCancellableStart();

    auto stop = coordinator.beginStop(
        WaitPolicy::no_wait);

    expect(
        !stop.owned(),
        "no-wait cancellation should not claim teardown");

    expect(
        !start.tryComplete(),
        "tryComplete() must fail after cancellation is claimed");

    expect(
        coordinator.state() == State::starting,
        "failed tryComplete() must retain startup ownership");

    start.fail();

    expect(
        coordinator.state() == State::stopped,
        "fail() should resolve startup after rollback");
}

void testTryCompleteWinsBeforeStop()
{
    LifecycleCoordinator coordinator;

    auto start = coordinator.beginCancellableStart();

    expect(
        start.tryComplete(),
        "tryComplete() should succeed before cancellation");

    auto stop = coordinator.beginStop();

    expect(
        stop.owned(),
        "stop should claim teardown after startup reaches running");

    stop.complete();

    expect(
        coordinator.state() == State::stopped,
        "completed teardown should reach stopped");
}

void testNoWaitDuringOrdinaryStartPreservesLegacyStartup()
{
    LifecycleCoordinator coordinator;

    auto start = coordinator.beginStart();

    auto stop = coordinator.beginStop(
        WaitPolicy::no_wait);

    expect(
        !stop.owned(),
        "no-wait stop cannot claim an ordinary active start");

    expect(
        coordinator.state() == State::starting,
        "ordinary startup should remain active");

    start.complete();

    expect(
        coordinator.state() == State::running,
        "ordinary startup should retain legacy behavior");

    auto finalStop = coordinator.beginStop();
    finalStop.complete();
}

void testStartOwnerThreadIdentity()
{
    LifecycleCoordinator coordinator;

    auto start = coordinator.beginCancellableStart();

    expect(
        coordinator.isStartOwnerThread(),
        "claiming thread should be recognized");

    auto otherThread = std::async(
        std::launch::async,
        [&] {
            return coordinator.isStartOwnerThread();
        });

    expect(
        !otherThread.get(),
        "another thread must not be recognized as start owner");

    expect(
        start.tryComplete(),
        "startup should complete normally");
}

void testNoWaitDuringActiveStopReturnsImmediately()
{
    LifecycleCoordinator coordinator;

    auto start = coordinator.beginStart();
    start.complete();

    auto firstStop = coordinator.beginStop();

    expect(
        firstStop.owned(),
        "first stop should own teardown");

    auto secondStop = coordinator.beginStop(
        WaitPolicy::no_wait);

    expect(
        !secondStop.owned(),
        "no-wait stop during teardown should return unowned");

    expect(
        coordinator.state() == State::stopping,
        "no-wait observer must not resolve active teardown");

    firstStop.complete();

    expect(
        coordinator.state() == State::stopped,
        "owning stop should complete teardown");
}

void testStateNeverWedgesAfterAbandonedTransitions()
{
    LifecycleCoordinator coordinator;

    for (int iteration = 0; iteration < 32; ++iteration) {
        {
            auto start = coordinator.beginStart();
        }

        expect(
            coordinator.isStopped(),
            "abandoned ordinary start must reach stopped");

        {
            auto start = coordinator.beginCancellableStart();
        }

        expect(
            coordinator.isStopped(),
            "abandoned cancellable start must reach stopped");

        auto start = coordinator.beginStart();
        start.complete();

        {
            auto stop = coordinator.beginStop();
        }

        expect(
            coordinator.isStopped(),
            "abandoned stop must reach stopped");
    }
}

} // namespace

int main()
{
    testStartFromStoppedClaimsStarting();
    testStartWhileRunningReturnsUnowned();
    testFailedStartReturnsToStopped();
    testAbandonedStartResolvesToStopped();
    testAbandonedStopResolvesToStopped();
    testFailedStopResolvesToStopped();
    testStopWhenStoppedReturnsUnowned();
    testConcurrentStopWaitsForTeardownCompletion();
    testStopDuringOrdinaryStartupWaitsAndClaims();
    testStopDuringFailedOrdinaryStartupDoesNotClaim();
    testStartDuringStoppingWaitsAndClaimsNewEpoch();

    testCancellableStartCompletesNormally();
    testCancellableStartWhileRunningReturnsUnowned();
    testAbandonedCancellableStartResolvesToStopped();
    testStopCancelsStartupAndWaitsForRollback();
    testNoWaitStopRequestsCancellationImmediately();
    testStopCallbackMayReenterCoordinator();
    testCancellationWinsBeforeTryComplete();
    testTryCompleteWinsBeforeStop();
    testNoWaitDuringOrdinaryStartPreservesLegacyStartup();
    testStartOwnerThreadIdentity();
    testNoWaitDuringActiveStopReturnsImmediately();

    testStateNeverWedgesAfterAbandonedTransitions();

    return EXIT_SUCCESS;
}
