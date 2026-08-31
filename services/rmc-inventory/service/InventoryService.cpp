#include "InventoryService.h"

#include <InotifyFileWatcher.h>

#include <InventoryIssueUtil.h>
#include <IWatchableInventorySource.h>
#include <inventory.hpp>

#include <glog/logging.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <poll.h>
#include <system_error>
#include <stdexcept>
#include <sys/eventfd.h>
#include <utility>
#include <vector>

namespace RSCGroup {
namespace {

[[nodiscard]] bool sourceStateTransitioned(const SourceState& lhs, const SourceState& rhs)
{
    return lhs.health != rhs.health || lhs.stale != rhs.stale || lhs.lastError != rhs.lastError;
}

[[nodiscard]] int msUntil(std::chrono::steady_clock::time_point ts)
{
    const auto now = std::chrono::steady_clock::now();
    if (ts <= now) {
        return 0;
    }
    return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(ts - now).count());
}

void signalFd(int fd)
{
    if (fd < 0) {
        return;
    }
    const std::uint64_t one = 1;
    const ssize_t rc = ::write(fd, &one, sizeof(one));
    (void)rc;
}

void drainFd(int fd)
{
    if (fd < 0) {
        return;
    }
    std::uint64_t value;
    const ssize_t rc = ::read(fd, &value, sizeof(value));
    (void)rc;
}

[[nodiscard]] std::vector<std::shared_ptr<IInventoryTransport>> inventoryTransports(const ServiceBase& service)
{
    std::vector<std::shared_ptr<IInventoryTransport>> typed;
    typed.reserve(service.transports().size());
    for (const auto& transport : service.transports()) {
        if (auto typedTransport = std::dynamic_pointer_cast<IInventoryTransport>(transport)) {
            typed.push_back(std::move(typedTransport));
        }
    }
    return typed;
}

} // namespace

std::unique_ptr<IFileWatcher> InventoryService::makeDefaultFileWatcher()
{
    return std::make_unique<InotifyFileWatcher>();
}

InventoryService::InventoryService(std::shared_ptr<IInventoryManager> manager,
                                   FileWatcherFactory fileWatcherFactory)
    : InventoryService(std::move(manager), Settings{}, std::move(fileWatcherFactory))
{
}

InventoryService::InventoryService(std::shared_ptr<IInventoryManager> manager,
                                   Settings settings,
                                   FileWatcherFactory fileWatcherFactory)
    : ServiceBase("inventory-service")
    , manager_(std::move(manager))
    , settings_(settings)
{
    if (!manager_) {
        throw std::invalid_argument("InventoryService: manager is null");
    }
    fileWatcher_ = fileWatcherFactory ? fileWatcherFactory() : makeDefaultFileWatcher();
    if (!fileWatcher_) {
        throw std::invalid_argument("InventoryService: file watcher factory returned null");
    }
}

InventoryService::~InventoryService()
{
    stop();
}

void InventoryService::addSource(std::shared_ptr<IInventorySource> source)
{
    if (!source) {
        throw std::invalid_argument("InventoryService::addSource: source is null");
    }

    std::scoped_lock lock(lifecycleMutex_);
    if (ServiceBase::isRunning()) {
        throw std::runtime_error("InventoryService::addSource: cannot add sources after start");
    }

    manager_->addSource(source);

    if (auto watchable = std::dynamic_pointer_cast<IWatchableInventorySource>(source)) {
        fileWatcher_->watchPath(watchable->getWatchPath());
    }
}

void InventoryService::addTransport(std::shared_ptr<IInventoryTransport> transport)
{
    if (!transport) {
        throw std::invalid_argument("InventoryService::addTransport: transport is null");
    }

    std::scoped_lock lock(lifecycleMutex_);
    if (ServiceBase::isRunning()) {
        throw std::runtime_error("InventoryService::addTransport: cannot add transports after start");
    }

    ServiceBase::addTransport(std::move(transport));
}

void InventoryService::validateConfiguration()
{
    // Manager is validated at construction time; nothing further to check.
}

bool InventoryService::initializeComponents()
{
    if (!refreshEventFd_) {
        const int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        refreshEventFd_.reset(fd);
        if (!refreshEventFd_) {
            LOG(ERROR) << "InventoryService: failed to create refresh eventfd";
            return false;
        }
    }

    for (const auto& transport : inventoryTransports(*this)) {
        transport->bindQueryService(*this);
    }

    return true;
}

bool InventoryService::start()
{
    std::scoped_lock lock(lifecycleMutex_);
    if (ServiceBase::isRunning()) {
        if (loopFailed_.load(std::memory_order_acquire)) {
            LOG(ERROR) << "InventoryService: loop thread is dead; call stop() before start()";
            throw std::logic_error("InventoryService: restart after crash requires stop() first");
        }
        return true;
    }
    if (loopThread_.joinable()) {
        throw std::logic_error("InventoryService: internal error: loop thread must be reaped by stop() before start()");
    }

    if (!ServiceBase::start()) {
        refreshEventFd_.reset();
        return false;
    }

    loopFailed_.store(false, std::memory_order_release);
    loopAlive_.store(true, std::memory_order_release);
    nextReconcileTs_ = std::chrono::steady_clock::now();
    try {
        loopThread_ = std::jthread([this](std::stop_token st) {
            try {
                runLoop(st);
            } catch (const std::exception& e) {
                loopFailed_.store(true, std::memory_order_release);
                LOG(ERROR) << "InventoryService: loop thread crashed: " << e.what();
            } catch (...) {
                loopFailed_.store(true, std::memory_order_release);
                LOG(ERROR) << "InventoryService: loop thread crashed with unknown exception";
            }
            loopAlive_.store(false, std::memory_order_release);
        });
    } catch (const std::exception& e) {
        loopAlive_.store(false, std::memory_order_release);
        loopFailed_.store(false, std::memory_order_release);
        refreshEventFd_.reset();
        ServiceBase::stop();
        LOG(ERROR) << "InventoryService: failed to start loop thread: " << e.what();
        return false;
    } catch (...) {
        loopAlive_.store(false, std::memory_order_release);
        loopFailed_.store(false, std::memory_order_release);
        refreshEventFd_.reset();
        ServiceBase::stop();
        LOG(ERROR) << "InventoryService: failed to start loop thread";
        return false;
    }
    return true;
}

void InventoryService::stop()
{
    std::scoped_lock lock(lifecycleMutex_);

    if (loopThread_.joinable()) {
        loopThread_.request_stop();
        signalFd(refreshEventFd_.get());
        loopThread_.join();
    }

    loopFailed_.store(false, std::memory_order_release);
    refreshEventFd_.reset();
    ServiceBase::stop();
}

bool InventoryService::isRunning() const
{
    return ServiceBase::isRunning();
}

interop_contract::inventory::InventorySnapshot InventoryService::getIdentity() const
{
    return manager_->getSnapshot();
}

InventoryFields InventoryService::getField(const std::string& fieldName) const
{
    return interop_contract::inventory::make_single_field_map(manager_->getSnapshot(), fieldName);
}

interop_contract::inventory::SourceStateMap InventoryService::getSourceStates() const
{
    return manager_->getSourceStates();
}

bool InventoryService::getReady() const
{
    return ServiceBase::isReady();
}

std::string InventoryService::getPhase() const
{
    return manager_->getPhase();
}

uint64_t InventoryService::getVersion() const
{
    return manager_->getVersion();
}

void InventoryService::refresh()
{
    {
        std::scoped_lock lock(refreshMutex_);
        refreshRequested_ = true;
    }
    signalFd(refreshEventFd_.get());
}

void InventoryService::runLoop(std::stop_token stopToken)
{
    doRefreshCycle(true);

    while (!stopToken.stop_requested()) {
        fileWatcher_->maintain();

        std::chrono::steady_clock::time_point wakeTs = nextReconcileTs_;

        {
            std::scoped_lock lock(refreshMutex_);
            if (refreshRequested_) {
                wakeTs = std::min(wakeTs, lastRefreshSteadyTs_ + settings_.minRefreshInterval);
            }
        }

        std::vector<pollfd> fds;
        fds.reserve(2);
        fds.push_back({refreshEventFd_.get(), POLLIN, 0});
        fds.push_back({fileWatcher_->getPollFd(), POLLIN, 0});

        const int pr = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), msUntil(wakeTs));
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::system_error(errno, std::generic_category(), "InventoryService: poll failed");
        }

        bool sourceTriggered = false;

        if (pr > 0) {
            if ((fds[0].revents & POLLIN) != 0) {
                drainFd(refreshEventFd_.get());
            }

            if ((fds[1].revents & POLLIN) != 0) {
                if (!fileWatcher_->consumeChangedPaths().empty()) {
                    sourceTriggered = true;
                }
            }
        }

        if (stopToken.stop_requested()) {
            break;
        }

        bool requestPending = false;
        {
            std::scoped_lock lock(refreshMutex_);
            requestPending = refreshRequested_;
        }

        if (requestPending || sourceTriggered) {
            doRefreshCycle(true);
        } else if (std::chrono::steady_clock::now() >= nextReconcileTs_) {
            doRefreshCycle(false);
        }
    }
}

void InventoryService::doRefreshCycle(bool force)
{
    const auto now = std::chrono::steady_clock::now();

    if (force && (now - lastRefreshSteadyTs_) < settings_.minRefreshInterval) {
        std::scoped_lock lock(refreshMutex_);
        refreshRequested_ = true;
        return;
    }

    {
        std::scoped_lock lock(refreshMutex_);
        refreshRequested_ = false;
    }

    const auto oldStates = manager_->getSourceStates();
    const bool oldReady = manager_->isReady();

    const InventoryDiff diff = manager_->refreshAll();

    const auto newStates = manager_->getSourceStates();
    const bool newReady = manager_->isReady();

    publishDiff(diff, oldStates, newStates, oldReady, newReady);

    lastRefreshSteadyTs_ = now;
    nextReconcileTs_ = now + settings_.reconcileInterval;
}

void InventoryService::publishDiff(const InventoryDiff& diff,
                                   const interop_contract::inventory::SourceStateMap& oldStates,
                                   const interop_contract::inventory::SourceStateMap& newStates,
                                   bool oldReady,
                                   bool newReady)
{
    const auto typedTransports = inventoryTransports(*this);

    for (const auto& field : diff.changedFields) {
        for (const auto& transport : typedTransports) {
            publishInventoryChange(transport, field);
        }
    }

    for (const auto& field : diff.removedFields) {
        for (const auto& transport : typedTransports) {
            publishInventoryChange(transport, field);
        }
    }

    for (const auto& [sourceName, newState] : newStates) {
        const auto oldIt = oldStates.find(sourceName);
        const bool transitioned = oldIt == oldStates.end() || sourceStateTransitioned(oldIt->second, newState);

        if (transitioned) {
            if (newState.health == SourceHealth::FAILED && newState.lastError) {
                LOG(ERROR) << "Inventory source '" << sourceName << "' failed: " << *newState.lastError;
            } else if (oldIt != oldStates.end() && oldIt->second.health == SourceHealth::FAILED &&
                       newState.health == SourceHealth::OK) {
                LOG(INFO) << "Inventory source '" << sourceName << "' recovered";
            }
            for (const auto& transport : typedTransports) {
                publishSourceStateChange(transport, sourceName);
            }
        }
    }

    if (oldReady != newReady) {
        ServiceBase::setReady(newReady);
    }
}

interop_contract::inventory::InventoryIssues InventoryService::getIssues() const
{
    auto issues = InventoryIssueUtil::deriveIssues(manager_->getSourceStates());

    if (loopFailed_.load(std::memory_order_acquire)) {
        InventoryFields issue;
        issue.emplace(std::string(interop_contract::inventory::ISSUE_SEVERITY),
                      std::string(interop_contract::inventory::SEVERITY_ERROR));
        issue.emplace(std::string(interop_contract::inventory::ISSUE_MESSAGE),
                      std::string("inventory refresh loop terminated unexpectedly; inventory data may be stale"));
        issue.emplace(std::string(interop_contract::inventory::ISSUE_ORIGIN),
                      std::string("InventoryService::runLoop"));
        issues["inventory.loop.stopped"] = std::move(issue);
    }

    return issues;
}

void InventoryService::publishInventoryChange(const std::shared_ptr<IInventoryTransport>& transport,
                                              const std::string& fieldName) const noexcept
{
    try {
        transport->publishInventoryChanged(fieldName);
    } catch (const std::exception& e) {
        LOG(ERROR) << "InventoryService: publishInventoryChanged failed for transport "
                   << transport->name() << " and field '" << fieldName << "': " << e.what();
    } catch (...) {
        LOG(ERROR) << "InventoryService: publishInventoryChanged failed for transport "
                   << transport->name() << " and field '" << fieldName << "'";
    }
}

void InventoryService::publishSourceStateChange(const std::shared_ptr<IInventoryTransport>& transport,
                                                const std::string& sourceName) const noexcept
{
    try {
        transport->publishSourceStateChanged(sourceName);
    } catch (const std::exception& e) {
        LOG(ERROR) << "InventoryService: publishSourceStateChanged failed for transport "
                   << transport->name() << " and source '" << sourceName << "': " << e.what();
    } catch (...) {
        LOG(ERROR) << "InventoryService: publishSourceStateChanged failed for transport "
                   << transport->name() << " and source '" << sourceName << "'";
    }
}

} // namespace RSCGroup
