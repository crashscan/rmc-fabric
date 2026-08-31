#include "InventoryService.h"

#include <IInventoryManager.h>
#include <IFileWatcher.h>
#include <IInventorySource.h>
#include <IWatchableInventorySource.h>

#include <sys/eventfd.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
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

class FakeInventoryManager final : public IInventoryManager {
public:
    void addSource(std::shared_ptr<IInventorySource> source) override
    {
        sources_.push_back(std::move(source));
    }

    InventoryDiff refreshAll() override
    {
        ++refreshCalls_;
        ready_ = true;
        ++snapshot_.version;
        snapshot_.ready = ready_;
        snapshot_.phase = "live";
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
    bool ready_{false};
    int refreshCalls_{0};
};

class FakeFileWatcher final : public IFileWatcher {
public:
    FakeFileWatcher()
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

    void maintain() override {}

    [[nodiscard]] std::vector<std::string> consumeChangedPaths() override
    {
        return {};
    }

    [[nodiscard]] size_t watchedCount() const { return watchedPaths_.size(); }

private:
    int fd_{-1};
    std::vector<std::string> watchedPaths_;
};

class FakeInventoryTransport final : public IInventoryTransport {
public:
    void bindQueryService(IInventoryQueryService& queryService) override
    {
        bound_ = &queryService;
    }

    void start() override
    {
        started_ = true;
    }

    void stop() override
    {
        stopped_ = true;
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
    [[nodiscard]] bool stopped() const { return stopped_.load(); }
    [[nodiscard]] bool bound() const { return bound_ != nullptr; }
    [[nodiscard]] int readyTrueCount() const { return readyTrueCount_.load(); }
    [[nodiscard]] int readyFalseCount() const { return readyFalseCount_.load(); }

private:
    IInventoryQueryService* bound_{nullptr};
    std::atomic<bool> started_{false};
    std::atomic<bool> stopped_{false};
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

void testStartStopBindsTransportAndPublishesReadiness()
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

    for (int i = 0; i < 40 && transport->readyTrueCount() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    expect(service.isRunning(), "service should report running after start");
    expect(transport->started(), "transport start() should be called");
    expect(transport->bound(), "transport bindQueryService() should be called");
    expect(transport->readyTrueCount() >= 1, "expected at least one ready=true publication");

    service.stop();

    expect(!service.isRunning(), "service should report stopped after stop");
    expect(transport->stopped(), "transport stop() should be called");
    expect(transport->readyFalseCount() >= 1, "expected ready=false publication on stop");
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

} // namespace

int main()
{
    testStartStopBindsTransportAndPublishesReadiness();
    testAddWatchableSourceRegistersWatchPath();
    return EXIT_SUCCESS;
}
