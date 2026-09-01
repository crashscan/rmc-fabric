#include "LifecycleCoordinator.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace {

using namespace RSCGroup;
using State = LifecycleCoordinator::State;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "LifecycleCoordinatorTests: " << message << '\n';
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
        cv_.wait(lock, [this] { return open_; });
    }

    [[nodiscard]] bool waitFor(std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, timeout, [this] { return open_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool open_{false};
};

void testStartFromStoppedClaimsStarting()
{
    LifecycleCoordinator coordinator;
    expect(coordinator.state() == State::stopped, "initial state should be stopped");

    auto transition = coordinator.beginStart();
    expect(transition.owned(), "beginStart() in stopped must claim the transition");
    expect(coordinator.state() == State::starting, "state should be starting while claimed");

    transition.complete();
    expect(coordinator.state() == State::running, "completed start should reach running");
    expect(coordinator.isRunning(), "isRunning() should be true");
}

void testStartWhileRunningReturnsUnowned()
{
    LifecycleCoordinator coordinator;
    auto start = coordinator.beginStart();
    start.complete();

    auto second = coordinator.beginStart();
    expect(!second.owned(), "beginStart() while running must return an unowned transition");
    expect(coordinator.state() == State::running, "an unowned transition must not change state");
}

void testFailedStartReturnsToStopped()
{
    LifecycleCoordinator coordinator;
    auto transition = coordinator.beginStart();
    transition.fail();
    expect(coordinator.state() == State::stopped, "failed start must resolve to stopped");
}

void testAbandonedStartResolvesToStopped()
{
    LifecycleCoordinator coordinator;
    {
        auto transition = coordinator.beginStart();
        expect(transition.owned(), "beginStart() should claim");
    }
    expect(coordinator.state() == State::stopped, "abandoned start must resolve to stopped");
}

void testAbandonedStopResolvesToStopped()
{
    LifecycleCoordinator coordinator;
    auto start = coordinator.beginStart();
    start.complete();

    {
        auto stop = coordinator.beginStop();
        expect(stop.owned(), "beginStop() in running should claim");
    }
    expect(coordinator.state() == State::stopped,
           "abandoned stop must resolve to stopped and never roll back to running");
}

void testFailedStopResolvesToStopped()
{
    LifecycleCoordinator coordinator;
    auto start = coordinator.beginStart();
    start.complete();

    auto stop = coordinator.beginStop();
    stop.fail();
    expect(coordinator.state() == State::stopped, "failed stop must still resolve to stopped");
}

void testStopWhenStoppedReturnsUnowned()
{
    LifecycleCoordinator coordinator;
    auto stop = coordinator.beginStop();
    expect(!stop.owned(), "beginStop() in stopped must return an unowned transition");
    expect(coordinator.state() == State::stopped, "state must remain stopped");
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
        expect(stop.owned(), "the first stop must own the teardown");
        teardownEntered.open();
        releaseTeardown.wait();
        teardownFinished.store(true);
        stop.complete();
    });

    teardownEntered.wait();

    auto second = std::async(std::launch::async, [&] {
        auto stop = coordinator.beginStop();
        expect(!stop.owned(), "the second stop must not own the teardown");
        secondSawCompletion.store(teardownFinished.load());
    });

    expect(second.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout,
           "the second stop must wait while teardown is active");

    releaseTeardown.open();
    first.get();
    second.get();

    expect(secondSawCompletion.load(),
           "the second stop must return only after the active teardown completed");
    expect(coordinator.state() == State::stopped, "state should be stopped");
}

void testStopDuringStartupWaitsAndThenClaims()
{
    LifecycleCoordinator coordinator;

    Gate startupEntered;
    Gate releaseStartup;

    auto starter = std::async(std::launch::async, [&] {
        auto start = coordinator.beginStart();
        expect(start.owned(), "beginStart() should claim");
        startupEntered.open();
        releaseStartup.wait();
        start.complete();
    });

    startupEntered.wait();

    std::atomic<bool> claimedStop{false};
    auto stopper = std::async(std::launch::async, [&] {
        auto stop = coordinator.beginStop();
        claimedStop.store(stop.owned());
        stop.complete();
    });

    expect(stopper.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout,
           "stop during startup must wait for the startup outcome");

    releaseStartup.open();
    starter.get();
    stopper.get();

    expect(claimedStop.load(), "stop must claim teardown once startup succeeded");
    expect(coordinator.state() == State::stopped, "state should be stopped");
}

void testStopDuringFailedStartupDoesNotClaim()
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
        claimedStop.store(stop.owned());
    });

    releaseStartup.open();
    starter.get();
    stopper.get();

    expect(!claimedStop.load(), "stop must not claim teardown after a failed startup");
    expect(coordinator.state() == State::stopped, "state should be stopped");
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
        claimedStart.store(restart.owned());
        restart.complete();
    });

    expect(starter.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout,
           "start during stopping must wait until stopped");

    releaseTeardown.open();
    stopper.get();
    starter.get();

    expect(claimedStart.load(), "start must claim a new epoch after teardown completes");
    expect(coordinator.state() == State::running, "state should be running again");
}

void testStateNeverWedgesAfterAbandonedTransitions()
{
    LifecycleCoordinator coordinator;
    for (int i = 0; i < 32; ++i) {
        { auto start = coordinator.beginStart(); }
        expect(coordinator.isStopped(), "abandoned start must always resolve to stopped");
        auto start = coordinator.beginStart();
        start.complete();
        { auto stop = coordinator.beginStop(); }
        expect(coordinator.isStopped(), "abandoned stop must always resolve to stopped");
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
    testStopDuringStartupWaitsAndThenClaims();
    testStopDuringFailedStartupDoesNotClaim();
    testStartDuringStoppingWaitsAndClaimsNewEpoch();
    testStateNeverWedgesAfterAbandonedTransitions();
    return EXIT_SUCCESS;
}
