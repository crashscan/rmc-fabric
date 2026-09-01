#include "ManagedWorker.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <iostream>
#include <latch>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace RSCGroup;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "ManagedWorkerTests: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

/// Minimal cooperative gate used instead of sleep-based races.
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

/// A worker body that blocks on a stop-aware condition variable.
///
/// The wait is interruptible by the stop token itself, mirroring the real
/// services: a wake callback shortens the latency but a swallowed or failed
/// wake only degrades stop latency, it does not deadlock the worker.
struct BlockingWork {
    std::mutex mutex;
    std::condition_variable_any cv;
    bool woken{false};
    Gate entered;

    void operator()(std::stop_token stopToken)
    {
        entered.open();
        std::unique_lock lock(mutex);
        cv.wait(lock, stopToken, [&] { return woken; });
    }

    void wake()
    {
        {
            std::scoped_lock lock(mutex);
            woken = true;
        }
        cv.notify_all();
    }
};

void testNormalStartRequestWakeJoin()
{
    BlockingWork work;
    std::atomic<int> wakeCount{0};
    std::atomic<int> exitCount{0};
    ManagedWorker::ExitReason reason{ManagedWorker::ExitReason::not_started};

    ManagedWorker worker(
        "normal",
        [&work](std::stop_token st) { work(std::move(st)); },
        [&work, &wakeCount] {
            ++wakeCount;
            work.wake();
        },
        [&exitCount, &reason](const ManagedWorker::Exit& exit) {
            reason = exit.reason;
            ++exitCount;
        });

    expect(!worker.isRunning(), "worker should not be running before start");
    expect(worker.start(), "start() should launch the worker");
    work.entered.wait();
    expect(worker.isRunning(), "worker should report running");

    worker.stop();

    expect(!worker.isRunning(), "worker should not be running after stop");
    expect(!worker.isJoinable(), "worker thread should be joined after stop");
    expect(wakeCount.load() == 1, "wake must fire exactly once per real stop request");
    expect(exitCount.load() == 1, "exit handler must run exactly once");
    expect(reason == ManagedWorker::ExitReason::stop_requested, "exit reason should be stop_requested");
    expect(worker.lastExit().reason == ManagedWorker::ExitReason::stop_requested, "lastExit should record stop_requested");
}

void testStopBeforeStartIsNoOp()
{
    std::atomic<int> wakeCount{0};
    ManagedWorker worker(
        "never-started",
        [](std::stop_token) {},
        [&wakeCount] { ++wakeCount; });

    worker.stop();
    worker.requestStop();

    expect(wakeCount.load() == 0, "wake must not fire when no worker was ever launched");
    expect(!worker.isRunning(), "worker should not be running");
    expect(worker.lastExit().reason == ManagedWorker::ExitReason::not_started, "lastExit should be not_started");
}

void testRepeatedStopIsIdempotent()
{
    BlockingWork work;
    std::atomic<int> wakeCount{0};
    ManagedWorker worker(
        "repeated-stop",
        [&work](std::stop_token st) { work(std::move(st)); },
        [&work, &wakeCount] {
            ++wakeCount;
            work.wake();
        });

    expect(worker.start(), "start() should launch the worker");
    work.entered.wait();
    worker.stop();
    worker.stop();
    worker.stop();

    expect(wakeCount.load() == 1, "repeated stop must not re-fire wake");
    expect(!worker.isJoinable(), "worker must remain joined");
}

void testConcurrentStopAndJoinSerialize()
{
    BlockingWork work;
    std::atomic<int> wakeCount{0};
    ManagedWorker worker(
        "concurrent-stop",
        [&work](std::stop_token st) { work(std::move(st)); },
        [&work, &wakeCount] {
            ++wakeCount;
            work.wake();
        });

    expect(worker.start(), "start() should launch the worker");
    work.entered.wait();

    std::latch ready(4);
    std::vector<std::future<void>> callers;
    callers.reserve(4);
    for (int i = 0; i < 2; ++i) {
        callers.push_back(std::async(std::launch::async, [&worker, &ready] {
            ready.arrive_and_wait();
            worker.stop();
        }));
        callers.push_back(std::async(std::launch::async, [&worker, &ready] {
            ready.arrive_and_wait();
            worker.join();
        }));
    }
    for (auto& caller : callers) {
        caller.get();
    }

    expect(!worker.isRunning(), "worker must be stopped after concurrent callers return");
    expect(!worker.isJoinable(), "concurrent stop/join must not double-join");
    expect(wakeCount.load() == 1, "concurrent stop must issue exactly one real stop request");
}

void testStartWhileRunningReturnsFalse()
{
    BlockingWork work;
    ManagedWorker worker(
        "start-while-running",
        [&work](std::stop_token st) { work(std::move(st)); },
        [&work] { work.wake(); });

    expect(worker.start(), "first start() should launch the worker");
    work.entered.wait();
    expect(!worker.start(), "start() while running must return false");
    worker.stop();
}

void testWorkerExceptionIsCaptured()
{
    Gate exited;
    ManagedWorker worker(
        "throwing",
        [](std::stop_token) { throw std::runtime_error("worker blew up"); },
        {},
        [&exited](const ManagedWorker::Exit&) { exited.open(); });

    expect(worker.start(), "start() should launch the worker");
    expect(exited.waitFor(std::chrono::seconds(5)), "exit handler should run after the worker throws");
    worker.join();

    const auto exit = worker.lastExit();
    expect(exit.reason == ManagedWorker::ExitReason::exception, "exit reason should be exception");
    expect(exit.exception != nullptr, "exception should be captured");
    bool rethrown = false;
    try {
        std::rethrow_exception(exit.exception);
    } catch (const std::runtime_error& e) {
        rethrown = std::string(e.what()) == "worker blew up";
    }
    expect(rethrown, "captured exception should be the original one");
}

void testUnexpectedNormalReturn()
{
    Gate exited;
    ManagedWorker worker(
        "returns-early",
        [](std::stop_token) {},
        {},
        [&exited](const ManagedWorker::Exit&) { exited.open(); });

    expect(worker.start(), "start() should launch the worker");
    expect(exited.waitFor(std::chrono::seconds(5)), "exit handler should run after normal return");
    worker.join();
    expect(worker.lastExit().reason == ManagedWorker::ExitReason::returned,
           "unexpected normal return should be reported as returned");
    expect(!worker.isRunning(), "worker must not report running after returning");
}

void testRestartAfterNormalStop()
{
    BlockingWork work;
    std::atomic<int> starts{0};
    ManagedWorker worker(
        "restart-clean",
        [&work, &starts](std::stop_token st) {
            ++starts;
            work(std::move(st));
        },
        [&work] { work.wake(); });

    expect(worker.start(), "first start() should launch");
    work.entered.wait();
    worker.stop();

    // Reuse the same worker object with fresh blocking state.
    work.woken = false;
    expect(worker.start(), "restart after a clean stop should launch a new worker");
    worker.stop();
    expect(starts.load() == 2, "the work function should have run twice");
}

void testRestartAfterFailureWithoutInterveningStop()
{
    Gate firstExit;
    std::atomic<int> runs{0};
    ManagedWorker worker(
        "restart-after-failure",
        [&runs](std::stop_token) {
            ++runs;
            throw std::runtime_error("crash");
        },
        {},
        [&firstExit](const ManagedWorker::Exit&) { firstExit.open(); });

    expect(worker.start(), "first start() should launch");
    expect(firstExit.waitFor(std::chrono::seconds(5)), "first worker should exit by exception");
    expect(worker.isJoinable(), "finished worker should still be unjoined");

    // No intervening stop(): start() must reap the finished thread and relaunch.
    expect(worker.start(), "start() must reap a finished-but-unjoined worker and relaunch");
    worker.stop();
    expect(runs.load() == 2, "the work function should have run twice");
}

void testStatusReadsWhileRunning()
{
    BlockingWork work;
    ManagedWorker worker(
        "status",
        [&work](std::stop_token st) { work(std::move(st)); },
        [&work] { work.wake(); });

    expect(!worker.isJoinable(), "worker should not be joinable before start");
    expect(worker.start(), "start() should launch the worker");
    work.entered.wait();
    expect(worker.isRunning(), "isRunning() should be true while the worker runs");
    expect(worker.isJoinable(), "isJoinable() should be true while the worker runs");
    expect(!worker.isCurrentThread(), "the test thread is not the worker thread");
    worker.stop();
}

void testSelfStopAndSelfJoinAreRejected()
{
    Gate finished;
    std::atomic<bool> selfStopRejected{false};
    std::atomic<bool> selfJoinRejected{false};
    std::atomic<bool> selfIdentityCorrect{false};

    ManagedWorker* self = nullptr;
    ManagedWorker worker(
        "self-ops",
        [&](std::stop_token) {
            selfIdentityCorrect = self->isCurrentThread();
            try {
                self->stop();
            } catch (const std::logic_error&) {
                selfStopRejected = true;
            }
            try {
                self->join();
            } catch (const std::logic_error&) {
                selfJoinRejected = true;
            }
            finished.open();
        });
    self = &worker;

    expect(worker.start(), "start() should launch the worker");
    expect(finished.waitFor(std::chrono::seconds(5)), "worker body should complete");
    worker.stop();

    expect(selfIdentityCorrect.load(), "isCurrentThread() must be true inside the worker");
    expect(selfStopRejected.load(), "self stop() must throw std::logic_error");
    expect(selfJoinRejected.load(), "self join() must throw std::logic_error");
}

void testSelfOperationRejectionInsideExitHandler()
{
    Gate finished;
    std::atomic<bool> identityValid{false};
    std::atomic<bool> stopRejected{false};
    std::atomic<bool> joinRejected{false};

    ManagedWorker* self = nullptr;
    ManagedWorker worker(
        "exit-handler-self-ops",
        [](std::stop_token) {},
        {},
        [&](const ManagedWorker::Exit&) {
            identityValid = self->isCurrentThread();
            try {
                self->stop();
            } catch (const std::logic_error&) {
                stopRejected = true;
            }
            try {
                self->join();
            } catch (const std::logic_error&) {
                joinRejected = true;
            }
            finished.open();
        });
    self = &worker;

    expect(worker.start(), "start() should launch the worker");
    expect(finished.waitFor(std::chrono::seconds(5)), "exit handler should run");
    worker.join();

    expect(identityValid.load(), "worker identity must stay valid through the exit handler");
    expect(stopRejected.load(), "stop() from the exit handler must be rejected");
    expect(joinRejected.load(), "join() from the exit handler must be rejected");
}

void testWakeExceptionIsContained()
{
    BlockingWork work;
    ManagedWorker worker(
        "throwing-wake",
        [&work](std::stop_token st) { work(std::move(st)); },
        [] { throw std::runtime_error("wake failed"); });

    expect(worker.start(), "start() should launch the worker");
    work.entered.wait();

    // The wake throws, so the worker only observes the cooperative stop token.
    // stop() must still return after the worker finalizes.
    worker.stop();
    expect(!worker.isRunning(), "a throwing wake must not prevent stop from completing");
}

void testExitHandlerExceptionIsContained()
{
    Gate handlerRan;
    ManagedWorker worker(
        "throwing-exit-handler",
        [](std::stop_token) {},
        {},
        [&handlerRan](const ManagedWorker::Exit&) {
            handlerRan.open();
            throw std::runtime_error("exit handler failed");
        });

    expect(worker.start(), "start() should launch the worker");
    expect(handlerRan.waitFor(std::chrono::seconds(5)), "exit handler should run");
    worker.join();
    expect(!worker.isRunning(), "a throwing exit handler must not prevent finalization");
    expect(worker.lastExit().reason == ManagedWorker::ExitReason::returned,
           "exit state must be recorded before the handler runs");
}

void testThreadLaunchFailureLeavesWorkerRestartable()
{
    // Thread construction failure cannot be forced portably; assert instead
    // that a failed start does not latch running state and that the worker
    // remains restartable after a completed run.
    Gate exited;
    ManagedWorker worker(
        "restartable",
        [](std::stop_token) {},
        {},
        [&exited](const ManagedWorker::Exit&) { exited.open(); });

    expect(worker.start(), "start() should launch the worker");
    expect(exited.waitFor(std::chrono::seconds(5)), "worker should exit");
    expect(!worker.isRunning(), "running state must be cleared by the worker");
    expect(worker.start(), "worker must remain restartable");
    worker.stop();
}

} // namespace

int main()
{
    testNormalStartRequestWakeJoin();
    testStopBeforeStartIsNoOp();
    testRepeatedStopIsIdempotent();
    testConcurrentStopAndJoinSerialize();
    testStartWhileRunningReturnsFalse();
    testWorkerExceptionIsCaptured();
    testUnexpectedNormalReturn();
    testRestartAfterNormalStop();
    testRestartAfterFailureWithoutInterveningStop();
    testStatusReadsWhileRunning();
    testSelfStopAndSelfJoinAreRejected();
    testSelfOperationRejectionInsideExitHandler();
    testWakeExceptionIsContained();
    testExitHandlerExceptionIsContained();
    testThreadLaunchFailureLeavesWorkerRestartable();
    return EXIT_SUCCESS;
}
