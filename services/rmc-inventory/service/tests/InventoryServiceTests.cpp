#include "InventoryService.h"

#include <IFileWatcher.h>
#include <IInventoryManager.h>
#include <IInventorySource.h>
#include <IInventoryTransport.h>
#include <IWatchableInventorySource.h>

#include <sys/eventfd.h>
#include <unistd.h>

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

class FakeInventoryManager final : public IInventoryManager {
public:
    explicit FakeInventoryManager(bool readyAfterRefresh = true)
        : readyAfterRefresh_(readyAfterRefresh)
    {
    }

    void addSource(std::shared_ptr<IInventorySource> source) override
    {
        sources_.push_back(std::move(source));
    }

    InventoryDiff refreshAll() override
    {
        ++refreshCalls_;
        ready_ = readyAfterRefresh_;
        ++snapshot_.version;
        snapshot_.ready = ready_;
        snapshot_.phase = ready_ ? "live" : "initializing";
        return InventoryDiff{{"identity.nodeName"}, {}};
    }

    [[nodiscard]] interop_contract::inventory::InventorySnapshot getSnapshot() const override
    {
        return snapshot_;
    }

    [[nodiscard]] interop_contract::inventory::SourceStateMap getSourceStates() const override
    {
        return {};
    }

    [[nodiscard]] bool isReady() const override { return ready_; }
    [[nodiscard]] std::string getPhase() const override { return snapshot_.phase; }
    [[nodiscard]] uint64_t getVersion() const override { return snapshot_.version; }

private:
    std::vector<std::shared_ptr<IInventorySource>> sources_;
    interop_contract::inventory::InventorySnapshot snapshot_{};
    bool readyAfterRefresh_{true};
    bool ready_{false};
    int refreshCalls_{0};
};

class FakeFileWatcher final : public IFileWatcher {
public:
    explicit FakeFileWatcher(bool blockMaintain = false)
        : blockMaintain_(blockMaintain)
    {
        fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    }

    ~FakeFileWatcher() override
    {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    void watchPath(const std::string& path) override
    {
        watchedPaths_.push_back(path);
    }

    [[nodiscard]] int getPollFd() const override
    {
        return fd_;
    }

    void maintain() override
    {
        if (!blockMaintain_) {
            return;
        }

        std::unique_lock lock(mutex_);
        maintainEntered_ = true;
        enteredCv_.notify_all();
        releaseCv_.wait(lock, [&] { return releaseMaintain_; });
    }

    [[nodiscard]] std::vector<std::string> consumeChangedPaths() override
    {
        return {};
    }

    [[nodiscard]] size_t watchedCount() const { return watchedPaths_.size(); }

    void waitUntilMaintainEntered()
    {
        std::unique_lock lock(mutex_);
        enteredCv_.wait(lock, [&] { return maintainEntered_; });
    }

    void releaseMaintain()
    {
        std::scoped_lock lock(mutex_);
        releaseMaintain_ = true;
        releaseCv_.notify_all();
    }

private:
    int fd_{-1};
    bool blockMaintain_{false};
    std::vector<std::string> watchedPaths_;
    mutable std::mutex mutex_;
    std::condition_variable enteredCv_;
    std::condition_variable releaseCv_;
    bool maintainEntered_{false};
    bool releaseMaintain_{false};
};

class FakeInventoryTransport final : public IInventoryTransport {
public:
    explicit FakeInventoryTransport(bool startResult = true)
        : startResult_(startResult)
    {
    }

    void bindQueryService(IInventoryQueryService& queryService) override
    {
        bound_ = &queryService;
    }

    bool start() override
    {
        ++startCount_;
        started_ = startResult_;
        return startResult_;
    }

    void stop() override
    {
        ++stopCount_;
    }

    [[nodiscard]] std::string name() const override
    {
        return startResult_ ? "fake" : "failing";
    }

    void publishInventoryChanged(const std::string&) override {}
    void publishSourceStateChanged(const std::string&) override {}

    void publishReadyChanged(bool ready) override
    {
        if (ready) {
            ++readyTrueCount_;
        } else {
            ++readyFalseCount_;
        }
    }

    [[nodiscard]] bool started() const { return started_.load(); }
    [[nodiscard]] bool bound() const { return bound_ != nullptr; }
    [[nodiscard]] int startCount() const { return startCount_.load(); }
    [[nodiscard]] int stopCount() const { return stopCount_.load(); }
    [[nodiscard]] int readyTrueCount() const { return readyTrueCount_.load(); }
    [[nodiscard]] int readyFalseCount() const { return readyFalseCount_.load(); }

private:
    bool startResult_{true};
    IInventoryQueryService* bound_{nullptr};
    std::atomic<bool> started_{false};
    std::atomic<int> startCount_{0};
    std::atomic<int> stopCount_{0};
    std::atomic<int> readyTrueCount_{0};
    std::atomic<int> readyFalseCount_{0};
};

class FakeWatchableSource final : public IInventorySource, public IWatchableInventorySource {
public:
    [[nodiscard]] std::string getName() const override { return "watchable"; }
    [[nodiscard]] bool isRequired() const override { return false; }
    [[nodiscard]] FieldNameList getOwnedFields() const override { return {}; }
    [[nodiscard]] InventoryFields collect() override { return {}; }
    [[nodiscard]] SourceState getState() const override { return SourceState{}; }
    [[nodiscard]] std::string getWatchPath() const override { return "/tmp/rmc-inventory-tests"; }
};

void testStartStopBindsTransportAndPublishesReadinessExactlyOnce()
{
    auto manager = std::make_shared<FakeInventoryManager>();
    auto transport = std::make_shared<FakeInventoryTransport>();

    InventoryService::Settings settings;
    settings.reconcileInterval = std::chrono::milliseconds(200);
    settings.minRefreshInterval = std::chrono::milliseconds(1);

    InventoryService service(manager, settings, [] {
        return std::make_unique<FakeFileWatcher>();
    });

    service.addTransport(transport);
    expect(service.start(), "service failed to start");
    expect(waitFor([&] { return transport->readyTrueCount() == 1; }), "expected exactly one ready=true publication");

    expect(service.isRunning(), "service should report running after start");
    expect(transport->started(), "transport start() should be called");
    expect(transport->bound(), "transport bindQueryService() should be called");

    service.stop();
    service.stop();

    expect(!service.isRunning(), "service should report stopped after stop");
    expect(transport->stopCount() == 1, "transport stop() should be called exactly once");
    expect(transport->readyFalseCount() == 1, "expected ready=false publication on stop");
}

void testAddWatchableSourceRegistersWatchPath()
{
    auto manager = std::make_shared<FakeInventoryManager>();
    FakeFileWatcher* watcher = nullptr;

    InventoryService service(manager, {}, [&watcher] {
        auto impl = std::make_unique<FakeFileWatcher>();
        watcher = impl.get();
        return impl;
    });

    service.addSource(std::make_shared<FakeWatchableSource>());
    expect(watcher != nullptr, "watcher factory did not provide watcher");
    expect(watcher->watchedCount() == 1, "watchable source should register exactly one watch path");
}

void testTransportStartFailureRollsBackWithoutRunningLoop()
{
    auto manager = std::make_shared<FakeInventoryManager>();
    auto first = std::make_shared<FakeInventoryTransport>(true);
    auto failing = std::make_shared<FakeInventoryTransport>(false);

    InventoryService service(manager, {}, [] {
        return std::make_unique<FakeFileWatcher>();
    });
    service.addTransport(first);
    service.addTransport(failing);

    expect(!service.start(), "service start should fail when a transport fails");
    expect(!service.isRunning(), "service should not be running after failed transport start");
    expect(first->startCount() == 1, "first transport should attempt start once");
    expect(first->stopCount() == 1, "started transport should be rolled back exactly once");
    expect(failing->startCount() == 1, "failing transport should attempt start once");
    expect(failing->stopCount() == 0, "failing transport should not be stopped when start returned false");
}

void testStopWaitsForWorkerBeforeStoppingTransports()
{
    auto manager = std::make_shared<FakeInventoryManager>();
    auto transport = std::make_shared<FakeInventoryTransport>();
    FakeFileWatcher* watcher = nullptr;

    InventoryService::Settings settings;
    settings.reconcileInterval = std::chrono::milliseconds(200);
    settings.minRefreshInterval = std::chrono::milliseconds(1);

    InventoryService service(manager, settings, [&watcher] {
        auto impl = std::make_unique<FakeFileWatcher>(true);
        watcher = impl.get();
        return impl;
    });
    service.addTransport(transport);

    expect(service.start(), "service failed to start with blocking watcher");
    expect(watcher != nullptr, "blocking watcher should be created");
    watcher->waitUntilMaintainEntered();

    auto stopFuture = std::async(std::launch::async, [&service] { service.stop(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    expect(transport->stopCount() == 0, "transport stop must wait for the worker loop to exit");

    watcher->releaseMaintain();
    stopFuture.get();
    expect(transport->stopCount() == 1, "transport should stop after the worker loop exits");
}

} // namespace

int main()
{
    testStartStopBindsTransportAndPublishesReadinessExactlyOnce();
    testAddWatchableSourceRegistersWatchPath();
    testTransportStartFailureRollsBackWithoutRunningLoop();
    testStopWaitsForWorkerBeforeStoppingTransports();
    return EXIT_SUCCESS;
}
