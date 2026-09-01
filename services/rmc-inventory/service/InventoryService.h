#pragma once

#include <LifecycleCoordinator.h>
#include <ManagedWorker.h>
#include <ServiceBase.h>
#include <ScopedFd.h>

#include <IFileWatcher.h>
#include <IInventoryManager.h>
#include <IInventoryQueryService.h>
#include <IInventorySource.h>
#include <IInventoryTransport.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>

namespace RSCGroup {

class InventoryService final : public ServiceBase, public IInventoryQueryService {
public:
    struct Settings {
        std::chrono::milliseconds reconcileInterval{60000};
        std::chrono::milliseconds minRefreshInterval{1000};
    };

    using FileWatcherFactory = std::function<std::unique_ptr<IFileWatcher>()>;

    explicit InventoryService(std::shared_ptr<IInventoryManager> manager,
                              FileWatcherFactory fileWatcherFactory = {});
    explicit InventoryService(std::shared_ptr<IInventoryManager> manager,
                              Settings settings,
                              FileWatcherFactory fileWatcherFactory = {});
    ~InventoryService() override;

    InventoryService(const InventoryService&) = delete;
    InventoryService& operator=(const InventoryService&) = delete;

    void addSource(std::shared_ptr<IInventorySource> source);
    void addTransport(std::shared_ptr<IInventoryTransport> transport);

    [[nodiscard]] bool initializeComponents() override;
    void validateConfiguration() override;

    [[nodiscard]] bool start() override;
    void stop() override;

    [[nodiscard]] bool isRunning() const;

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
    void onRefreshWorkerExit(const ManagedWorker::Exit& exit);
    void doRefreshCycle(bool force);
    void publishDiff(const InventoryDiff& diff,
                     const interop_contract::inventory::SourceStateMap& oldStates,
                     const interop_contract::inventory::SourceStateMap& newStates,
                     bool oldReady,
                     bool newReady);
    void publishInventoryChange(const std::shared_ptr<IInventoryTransport>& transport,
                                const std::string& fieldName) const noexcept;
    void publishSourceStateChange(const std::shared_ptr<IInventoryTransport>& transport,
                                  const std::string& sourceName) const noexcept;

    static std::unique_ptr<IFileWatcher> makeDefaultFileWatcher();

    std::shared_ptr<IInventoryManager> manager_;
    std::unique_ptr<IFileWatcher> fileWatcher_;
    Settings settings_;

    /// Inventory-owned worker health state (Policy A).  The lifecycle
    /// coordinator never decides worker health.
    std::atomic<bool> loopFailed_{false};

    mutable std::mutex refreshMutex_;
    bool refreshRequested_{false};

    std::chrono::steady_clock::time_point lastRefreshSteadyTs_{};
    std::chrono::steady_clock::time_point nextReconcileTs_{};

    ScopedFd refreshEventFd_;

    /// Serializes complete service-epoch transitions only.
    LifecycleCoordinator lifecycle_;

    // ------------------------------------------------------------------ //
    // Member destruction order
    // ------------------------------------------------------------------ //
    // refreshWorker_ MUST be the last member: its work/wake/exit callbacks
    // capture `this` and access manager_, fileWatcher_, settings_,
    // loopFailed_, refreshMutex_, refreshRequested_, the reconcile
    // timestamps, and refreshEventFd_.  Declaring it last means reverse
    // member destruction destroys (and therefore stops and joins) the worker
    // before any state it touches goes away.
    ManagedWorker refreshWorker_;
};

} // namespace RSCGroup
