//
// Created by vvass on 24-Jul-26.
//
#include "LldpdSource.h"
#include "LldpUtils.h"
#include <lldpctl.h>
#include <lldpctl.hpp>
#include <glog/logging.h>
#include <chrono>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>

namespace RSCGroup {

namespace {

struct CachedLldpNeighbor {
    std::optional<std::string> rawChassisId;
    std::optional<std::string> rawPortId;
    std::optional<std::string> rawSystemName;
};

/**
 * @brief Shared callback state owned by shared_ptr.
 *
 * All state touched by the external watch callback lives here.  The watch
 * callback captures only a weak_ptr to this object so that delayed or
 * copied callbacks cannot access freed source/service state.
 */
struct CallbackState {
    // Immutable after construction
    LldpSourceConfig config;
    LldpObservationCallback downstream;

    // Admission gate
    std::mutex       mtx;
    std::condition_variable cv;
    bool admitting   = false;
    int  activeCount = 0;

    // Neighbor cache
    mutable std::mutex cacheMutex;
    std::unordered_map<std::string,
        std::unordered_map<std::string, CachedLldpNeighbor>> byInterface;

    CallbackState(LldpSourceConfig cfg, LldpObservationCallback cb)
        : config(std::move(cfg)), downstream(std::move(cb)) {}
};

/**
 * @brief Move-only RAII lease that decrements the active callback count on
 *        every exit path including exceptions.
 */
class CallbackLease {
public:
    explicit CallbackLease(std::shared_ptr<CallbackState> state)
        : state_(std::move(state)) {}

    ~CallbackLease() noexcept { release(); }

    CallbackLease(CallbackLease&& o) noexcept : state_(std::move(o.state_)) {}
    CallbackLease& operator=(CallbackLease&& o) noexcept
    {
        if (this != &o) {
            release();
            state_ = std::move(o.state_);
        }
        return *this;
    }

    CallbackLease(const CallbackLease&)            = delete;
    CallbackLease& operator=(const CallbackLease&) = delete;

private:
    void release() noexcept
    {
        if (!state_) return;
        {
            std::unique_lock lk(state_->mtx);
            --state_->activeCount;
        }
        state_->cv.notify_all();
        state_.reset();
    }

    std::shared_ptr<CallbackState> state_;
};

/**
 * @brief Try to acquire a callback lease through the admission gate.
 *
 * Returns std::nullopt if admission is closed (stop/refresh in progress).
 */
[[nodiscard]] std::optional<CallbackLease> tryAcquireLease(
    const std::shared_ptr<CallbackState>& state)
{
    std::unique_lock lk(state->mtx);
    if (!state->admitting) return std::nullopt;
    ++state->activeCount;
    return CallbackLease(state);
}

/**
 * @brief Open admission on a CallbackState.
 *
 * Must be called before creating the watch handle so that initial
 * enumeration callbacks (which may fire synchronously) are admitted.
 */
void openAdmission(CallbackState& state) noexcept
{
    std::unique_lock lk(state.mtx);
    state.admitting = true;
}

/**
 * @brief Close admission and wait for all active leases to drain.
 *
 * Does NOT hold the mutex while waiting, so it cannot deadlock with
 * callbacks that use the same lock.
 */
void closeAdmissionAndDrain(CallbackState& state) noexcept
{
    {
        std::unique_lock lk(state.mtx);
        state.admitting = false;
    }
    {
        std::unique_lock lk(state.mtx);
        state.cv.wait(lk, [&state] { return state.activeCount == 0; });
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// LldpdSource::Impl
// ---------------------------------------------------------------------------

class LldpdSource::Impl {
public:
    Impl(LldpSourceConfig config, LldpObservationCallback cb)
        : callbackState_(
              std::make_shared<CallbackState>(std::move(config), std::move(cb)))
    {}

    ~Impl()
    {
        try {
            stop();
        } catch (...) {
            LOG(ERROR) << "LldpdSource::Impl::~Impl: exception during stop (ignored)";
        }
    }

    bool start()
    {
        std::unique_lock lk(lifecycleMutex_);
        if (state_ == State::Running || state_ == State::Starting) return true;
        if (state_ != State::Stopped) return false;  // stopping or refreshing
        state_ = State::Starting;
        lk.unlock();

        // Open admission BEFORE creating watch so synchronous callbacks admitted
        openAdmission(*callbackState_);

        bool ok = makeWatch();
        if (!ok) {
            closeAdmissionAndDrain(*callbackState_);
            lk.lock();
            state_ = State::Stopped;
            return false;
        }

        enumerateInitialNeighbors();

        lk.lock();
        state_ = State::Running;
        LOG(INFO) << "LldpdSource started (push via lldpctl_watch)";
        return true;
    }

    void stop()
    {
        // 1. Claim stopping under lifecycleMutex_
        std::unique_lock lk(lifecycleMutex_);
        if (state_ == State::Stopped) return;
        if (state_ == State::Stopping) {
            // Another thread is already stopping — wait for it
            lifecycleCv_.wait(lk, [this] { return state_ == State::Stopped; });
            return;
        }
        state_ = State::Stopping;

        // 2. Close callback admission
        {
            std::unique_lock cbLk(callbackState_->mtx);
            callbackState_->admitting = false;
        }

        // 3. Move watch handle out of shared state
        auto watchToDestroy = std::move(watch_);

        // 4. Release lifecycle mutex before any blocking work
        lk.unlock();

        // 5. Destroy watch outside the lifecycle mutex (may block briefly)
        watchToDestroy.reset();

        // 6. Wait for active callback leases to drain
        {
            std::unique_lock cbLk(callbackState_->mtx);
            callbackState_->cv.wait(cbLk,
                [this] { return callbackState_->activeCount == 0; });
        }

        // 7. Clear cache only after drain completes
        {
            std::unique_lock cacheLk(callbackState_->cacheMutex);
            callbackState_->byInterface.clear();
        }

        // 8. Commit stopped and notify lifecycle waiters
        lk.lock();
        state_ = State::Stopped;
        lk.unlock();
        lifecycleCv_.notify_all();

        LOG(INFO) << "LldpdSource stopped";
    }

    [[nodiscard]] bool isRunning() const
    {
        std::unique_lock lk(lifecycleMutex_);
        return state_ == State::Running;
    }

    /**
     * @brief Reconnect: close old watch, drain old callbacks, reopen with new
     *        watch.  If the reconnect fails, transitions to Stopped.
     */
    void refreshAll()
    {
        // 1. running -> refreshing
        std::unique_lock lk(lifecycleMutex_);
        if (state_ != State::Running) return;
        state_ = State::Refreshing;

        // 2. Close admission
        {
            std::unique_lock cbLk(callbackState_->mtx);
            callbackState_->admitting = false;
        }

        // 3. Move old watch out
        auto oldWatch = std::move(watch_);

        lk.unlock();

        // 4. Destroy old watch outside mutex
        oldWatch.reset();

        // 5. Drain old callbacks
        {
            std::unique_lock cbLk(callbackState_->mtx);
            callbackState_->cv.wait(cbLk,
                [this] { return callbackState_->activeCount == 0; });
        }

        // 6. Clear cache
        {
            std::unique_lock cacheLk(callbackState_->cacheMutex);
            callbackState_->byInterface.clear();
        }

        // 7. Reopen admission
        openAdmission(*callbackState_);

        // 8. Create new watch and enumerate
        bool ok = makeWatch();
        if (ok) {
            enumerateInitialNeighbors();
        } else {
            closeAdmissionAndDrain(*callbackState_);
        }

        // 9. Commit state
        lk.lock();
        state_ = ok ? State::Running : State::Stopped;
        if (!ok) {
            LOG(ERROR) << "LldpdSource reconnection failed";
            lk.unlock();
            lifecycleCv_.notify_all();
            return;
        }
        LOG(INFO) << "LldpdSource reconnected";
    }

    void refreshInterface(const std::string& /*ifname*/) {}

    /**
     * @brief Flush all cached neighbors for a removed interface.
     *
     * Holds one admission lease for the entire batch so stop() cannot
     * interleave between partial removals.  If admission is closed
     * (stop/refresh in progress) the batch is discarded.
     */
    void removeInterface(const std::string& ifname)
    {
        auto lease = tryAcquireLease(callbackState_);
        if (!lease) return;  // stop/refresh in progress; discard batch

        std::vector<CachedLldpNeighbor> toRemove;
        {
            std::unique_lock cacheLk(callbackState_->cacheMutex);
            auto it = callbackState_->byInterface.find(ifname);
            if (it == callbackState_->byInterface.end()) return;
            for (const auto& [_, entry] : it->second)
                toRemove.push_back(entry);
            callbackState_->byInterface.erase(it);
        }

        for (const auto& entry : toRemove) {
            LldpObservation obs;
            obs.observedAt    = std::chrono::steady_clock::now();
            obs.kind          = ObservationKind::Lldp;
            obs.localIfname   = ifname;
            obs.event         = ObservationEvent::Removed;
            obs.remoteChassisId  = entry.rawChassisId;
            obs.remotePortId     = entry.rawPortId;
            obs.remoteSystemName = entry.rawSystemName;
            if (callbackState_->downstream) callbackState_->downstream(obs);
        }
        // lease released here, decrementing activeCount
    }

    /**
     * @brief Test seam: inject a parsed neighbor change directly through the
     *        admission gate, without a real lldpd daemon.
     *
     * The observation is dispatched through the same admission barrier as
     * live watch callbacks.  If admission is closed the call is a no-op.
     */
    void submitNeighborChangeForTest(std::string_view ifname,
                                     ObservationEvent event,
                                     std::optional<std::string> chassisId,
                                     std::optional<std::string> portId,
                                     std::optional<std::string> systemName)
    {
        auto lease = tryAcquireLease(callbackState_);
        if (!lease) return;

        LldpObservation obs;
        obs.observedAt   = std::chrono::steady_clock::now();
        obs.kind         = ObservationKind::Lldp;
        obs.localIfname  = std::string(ifname);
        obs.event        = event;
        obs.remoteChassisId  = std::move(chassisId);
        obs.remotePortId     = std::move(portId);
        obs.remoteSystemName = std::move(systemName);

        std::string key = resolveLldpIdentity(obs.remoteChassisId, obs.remotePortId);
        if (!key.empty()) {
            std::unique_lock cacheLk(callbackState_->cacheMutex);
            auto& ifaceCache = callbackState_->byInterface[std::string(ifname)];
            if (obs.event == ObservationEvent::Removed) {
                ifaceCache.erase(key);
            } else {
                ifaceCache[key] = CachedLldpNeighbor{
                    obs.remoteChassisId,
                    obs.remotePortId,
                    obs.remoteSystemName
                };
            }
        }

        if (callbackState_->downstream) callbackState_->downstream(obs);
        // lease released here
    }

private:
    bool makeWatch()
    {
        // Capture only a weak_ptr — the watch callback must not retain ownership
        // of the Impl or the shared CallbackState.
        std::weak_ptr<CallbackState> weakState = callbackState_;

        try {
            watch_ = std::make_unique<lldpcli::LldpWatch<void, void>>(
                std::make_optional<lldpcli::LldpWatch<void, void>::ChangeCallback<void>>(
                    [weakState](std::string_view ifname,
                                lldpctl_change_t change,
                                const lldpcli::LldpAtom& /*interface*/,
                                const lldpcli::LldpAtom& neighbor,
                                void* /*ctx*/) {
                        // Lock weak_ptr — if Impl is destroyed this is a no-op
                        auto state = weakState.lock();
                        if (!state) return;

                        auto lease = tryAcquireLease(state);
                        if (!lease) return;  // admission closed

                        try {
                            dispatchChange(*state, ifname, change, neighbor);
                        } catch (const std::exception& e) {
                            LOG(ERROR) << "LldpdSource: watch callback exception: " << e.what();
                        } catch (...) {
                            LOG(ERROR) << "LldpdSource: watch callback unknown exception";
                        }
                        // lease released here, decrementing activeCount
                    }
                )
            );
            return true;
        } catch (const std::exception& e) {
            LOG(ERROR) << "Lldpd watch creation failed: " << e.what();
            return false;
        } catch (...) {
            LOG(ERROR) << "Lldpd watch creation failed: unknown exception";
            return false;
        }
    }

    void enumerateInitialNeighbors()
    {
        // Precondition: admission is open; lifecycleMutex_ NOT held here
        try {
            lldpcli::LldpCtl ctl;
            for (const auto& iface : ctl.GetInterfaces()) {
                auto ifname = iface.GetValue<std::string>(lldpctl_k_interface_name);
                if (!ifname) continue;

                if (!callbackState_->config.watchedInterfaces.empty()) {
                    auto it = std::find(callbackState_->config.watchedInterfaces.begin(),
                                        callbackState_->config.watchedInterfaces.end(),
                                        *ifname);
                    if (it == callbackState_->config.watchedInterfaces.end()) continue;
                }

                auto port = iface.GetPort();
                auto neighbors = port.GetAtomList(lldpctl_k_port_neighbors);

                for (const auto& nb : neighbors) {
                    auto lease = tryAcquireLease(callbackState_);
                    if (!lease) return;  // stop/refresh began during enumeration
                    try {
                        dispatchChange(*callbackState_, *ifname, lldpctl_c_added, nb);
                    } catch (const std::exception& e) {
                        LOG(ERROR) << "LLDP initial enumeration dispatch error: " << e.what();
                    }
                }
            }
            VLOG(1) << "LLDP initial enumeration complete";
        } catch (const std::exception& e) {
            LOG(ERROR) << "LLDP initial enumeration failed: " << e.what();
        }
    }

    /**
     * @brief Process a single parsed change event against the given state.
     *
     * Caller must hold a valid CallbackLease (keeping activeCount > 0).
     * Cache locks are released before the downstream callback.
     */
    static void dispatchChange(CallbackState& state,
                               std::string_view ifname,
                               lldpctl_change_t change,
                               const lldpcli::LldpAtom& neighbor)
    {
        if (!state.config.watchedInterfaces.empty()) {
            auto it = std::find(state.config.watchedInterfaces.begin(),
                                state.config.watchedInterfaces.end(),
                                ifname);
            if (it == state.config.watchedInterfaces.end()) return;
        }

        if (change == lldpctl_c_deleted && !state.config.emitRemovals) return;

        LldpObservation obs;
        obs.observedAt   = std::chrono::steady_clock::now();
        obs.kind         = ObservationKind::Lldp;
        obs.localIfname  = std::string(ifname);
        obs.event        = (change == lldpctl_c_deleted)
            ? ObservationEvent::Removed
            : ObservationEvent::Present;

        auto chassisId = neighbor.GetValue<std::string>(lldpctl_k_chassis_id);
        if (chassisId) obs.remoteChassisId = *chassisId;

        auto portId = neighbor.GetValue<std::string>(lldpctl_k_port_id);
        if (portId) obs.remotePortId = *portId;

        auto chassisName = neighbor.GetValue<std::string>(lldpctl_k_chassis_name);
        if (chassisName) obs.remoteSystemName = *chassisName;

        std::string key = resolveLldpIdentity(obs.remoteChassisId, obs.remotePortId);
        if (key.empty()) {
            VLOG(1) << "LLDP neighbor on " << ifname
                    << " — non-MAC identity, not cached";
        } else {
            std::unique_lock cacheLk(state.cacheMutex);
            auto& ifaceCache = state.byInterface[std::string(ifname)];
            if (obs.event == ObservationEvent::Removed) {
                VLOG(1) << "LLDP cache erase: ifname=" << ifname << " key=" << key;
                ifaceCache.erase(key);
            } else {
                VLOG(1) << "LLDP cache insert: ifname=" << ifname << " key=" << key;
                ifaceCache[key] = CachedLldpNeighbor{
                    obs.remoteChassisId,
                    obs.remotePortId,
                    obs.remoteSystemName
                };
            }
            // release cache lock before downstream callback
        }

        if (state.downstream) state.downstream(obs);
    }

    enum class State { Stopped, Starting, Running, Refreshing, Stopping };

    std::shared_ptr<CallbackState> callbackState_;
    std::unique_ptr<lldpcli::LldpWatch<void, void>> watch_;

    mutable std::mutex lifecycleMutex_;
    std::condition_variable lifecycleCv_;
    State state_ = State::Stopped;
};

// ---------------------------------------------------------------------------
// LldpdSource public interface — thin delegation to Impl
// ---------------------------------------------------------------------------

LldpdSource::LldpdSource(LldpSourceConfig config, LldpObservationCallback cb)
    : impl_(std::make_unique<Impl>(std::move(config), std::move(cb))) {}

LldpdSource::~LldpdSource() = default;

bool LldpdSource::start()   { return impl_->start(); }
void LldpdSource::stop()    { impl_->stop(); }
bool LldpdSource::isRunning() const { return impl_->isRunning(); }
void LldpdSource::refreshAll()      { impl_->refreshAll(); }
void LldpdSource::refreshInterface(const std::string& ifname) { impl_->refreshInterface(ifname); }
void LldpdSource::removeInterface(const std::string& ifname)  { impl_->removeInterface(ifname); }

void LldpdSource::submitNeighborChangeForTest(std::string_view ifname,
                                              ObservationEvent event,
                                              std::optional<std::string> chassisId,
                                              std::optional<std::string> portId,
                                              std::optional<std::string> systemName)
{
    impl_->submitNeighborChangeForTest(ifname, event,
        std::move(chassisId), std::move(portId), std::move(systemName));
}

} // namespace RSCGroup