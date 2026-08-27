#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <core/IInventoryManager.h>
#include <api/IInventoryQueryService.h>
#include <api/ITransport.h>
#include <IFileWatcher.h>
#include <core/IInventorySource.h>

namespace RSCGroup {

// Lifecycle policy:
//   start()  – launches the refresh loop; no-op if already running;
//              throws if the loop previously crashed (call stop() first).
//   Crash    – worker exits, loopFailed_ is set, "inventory.loop.stopped"
//              is reported; service stays in 'running' (degraded) state.
//   Recovery – requires explicit stop() then start().
//   stop()   – idempotent; safe after a crash (joins the dead thread).
//
// Operational contract (v1):
//   GetReady()  – LATCHED. True once inventory has been successfully
//                 collected from all required sources at least once.
//                 Reflects "the data being served was validly obtained",
//                 NOT "the pipeline is currently functioning".
//                 Loop thread death does NOT clear readiness.
//
//   GetPhase()  – Last known pipeline phase. Not updated after loop
//                 death; v1 defines no "degraded" phase.
//
//   GetIssues() – The ONLY machine-readable signal for pipeline liveness.
//                 Loop death is reported as "inventory.loop.stopped"
//                 (ERROR) and means served data may be arbitrarily stale.
//
//   Liveness    – Query methods are served from latched in-memory state and
//                 MUST NOT depend on the loop thread being alive.
//
// Consumers requiring freshness MUST combine signals: GetReady() && !hasIssue("inventory.loop.stopped").
class InventoryService final : public IInventoryQueryService {
public:
    struct Settings {
        Settings(){};
        std::chrono::milliseconds reconcileInterval{std::chrono::milliseconds(60000)};
        std::chrono::milliseconds minRefreshInterval{std::chrono::milliseconds(1000)};
    };

    // fileWatcherFactory: optional; when empty, an InotifyFileWatcher is created.
    // Invoked once during construction; must not return null
    // (throws std::invalid_argument).
    using FileWatcherFactory = std::function<std::unique_ptr<IFileWatcher>()>;

    explicit InventoryService(std::shared_ptr<IInventoryManager> manager,
                              Settings settings = {},
                              FileWatcherFactory fileWatcherFactory = {});
    ~InventoryService() override;

    InventoryService(const InventoryService&) = delete;
    InventoryService& operator=(const InventoryService&) = delete;

    void addSource(std::shared_ptr<IInventorySource> source);
    void addTransport(std::shared_ptr<ITransport> transport);

    void start();
    void stop();

    [[nodiscard]] bool isRunning() const;

    // IInventoryQueryService
    [[nodiscard]] interop_contract::inventory::InventorySnapshot getIdentity() const override;
    [[nodiscard]] InventoryFields getField(const std::string& fieldName) const override;
    [[nodiscard]] interop_contract::inventory::SourceStateMap getSourceStates() const override;
    [[nodiscard]] bool getReady() const override;
    [[nodiscard]] std::string getPhase() const override;
    [[nodiscard]] uint64_t getVersion() const override;
    [[nodiscard]] interop_contract::inventory::InventoryIssues getIssues() const override;
    void refresh() override;

private:
    void runLoop(std::stop_token stopToken);

    // Loop-thread only
    void doRefreshCycle(bool force);
    void publishDiff(const InventoryDiff& diff,
                     const interop_contract::inventory::SourceStateMap& oldStates,
                     const interop_contract::inventory::SourceStateMap& newStates,
                     bool oldReady,
                     bool newReady);

private:
    static std::unique_ptr<IFileWatcher> makeDefaultFileWatcher();

    std::shared_ptr<IInventoryManager> manager_;
    std::unique_ptr<IFileWatcher> fileWatcher_;
    Settings settings_;

    std::vector<std::shared_ptr<ITransport>> transports_;

    // Service intent: written only by start()/stop().
    std::atomic<bool> running_{false};

    // Loop operational state: written by the worker thread, observed elsewhere.
    std::atomic<bool> loopAlive_{false};
    std::atomic<bool> loopFailed_{false};

    mutable std::mutex lifecycleMutex_;
    std::jthread loopThread_;

    mutable std::mutex refreshMutex_;
    bool refreshRequested_{false};

    std::chrono::steady_clock::time_point lastRefreshSteadyTs_{};
    std::chrono::steady_clock::time_point nextReconcileTs_{};

    // Wakeup fd for poll(): refresh requests + stop.
    int refreshEventFd_{-1};
};

} // namespace RSCGroup
