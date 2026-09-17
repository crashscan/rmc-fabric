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
#include <cstdint>

#include "BoundedLldpConnection.h"
#include "BoundedLldpWatch.h"
#include "LldpObservationFactory.h"

namespace RSCGroup {
namespace {
    // tick() is documented as bounded; the probe worst case is
    // connect (1s) + one round-trip (2s) ≈ 3s per supervision interval.
    constexpr auto kProbeConnectTimeout = std::chrono::milliseconds{1000};
    constexpr auto kProbeIoTimeout = std::chrono::milliseconds{2000};
    // Upper bound on waiting for in-flight callback leases during a
    // reconnect. refreshAll() runs on the supervision thread, which the
    // service joins BEFORE stopping the netlink monitor; an unbounded wait
    // here would deadlock shutdown against a slow netlink-originated
    // removeInterface(). stop() has no such bound because the monitor is
    // already stopped by then.
    constexpr auto kRefreshDrainTimeout = std::chrono::seconds{3};

    using NeighborCache = std::unordered_map<std::string, std::unordered_map<std::string, CachedLldpNeighbor> >;

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
        std::mutex mtx;
        std::condition_variable cv;
        bool admitting = false;
        int activeCount = 0;

        // Neighbor cache
        mutable std::mutex cacheMutex;
        NeighborCache byInterface;
        // Bumped on EVERY cache mutation (cacheAndForward, removeInterface,
        // stop() clear, refreshAll() swap) while cacheMutex is held. Read
        // lock-free by reassertAll() to detect a stale snapshot mid-delivery.
        std::atomic<std::uint64_t> cacheGeneration{0};

        // Backend liveness: stamped ONLY by backend-originated paths (watch
        // callbacks, initial enumeration). reassertAll() and removeInterface()
        // deliberately bypass stamping — keepalives must not feed the watchdog.
        std::atomic<std::chrono::steady_clock::time_point> lastWatchEventAt{
            std::chrono::steady_clock::time_point::min()
        };

        CallbackState(LldpSourceConfig cfg, LldpObservationCallback cb)
            : config(std::move(cfg)), downstream(std::move(cb)) {
        }
    };

    /**
     * @brief Move-only RAII lease that decrements the active callback count on
     *        every exit path including exceptions.
     */
    class CallbackLease {
    public:
        explicit CallbackLease(std::shared_ptr<CallbackState> state)
            : state_(std::move(state)) {
        }

        ~CallbackLease() noexcept { release(); }

        CallbackLease(CallbackLease &&o) noexcept : state_(std::move(o.state_)) {
        }

        CallbackLease &operator=(CallbackLease &&o) noexcept {
            if (this != &o) {
                release();
                state_ = std::move(o.state_);
            }
            return *this;
        }

        CallbackLease(const CallbackLease &) = delete;

        CallbackLease &operator=(const CallbackLease &) = delete;

    private:
        void release() noexcept {
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
        const std::shared_ptr<CallbackState> &state) {
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
    void openAdmission(CallbackState &state) noexcept {
        std::unique_lock lk(state.mtx);
        state.admitting = true;
    }

    /**
     * @brief Close admission and wait for all active leases to drain.
     *
     * Does NOT hold the mutex while waiting, so it cannot deadlock with
     * callbacks that use the same lock.
     */
    void closeAdmissionAndDrain(CallbackState &state) noexcept {
        {
            std::unique_lock lk(state.mtx);
            state.admitting = false;
        }
        {
            std::unique_lock lk(state.mtx);
            state.cv.wait(lk, [&state] { return state.activeCount == 0; });
        }
    }

    /**
     * @brief Cache-update + downstream delivery shared by the watch path
     *        (dispatchChange) and the test seam.
     *
     * Caller must hold a valid CallbackLease. The cache lock is released before
     * the downstream callback. Does not stamp liveness — callers stamp
     * themselves when backend-originated.
     */
    void cacheAndForward(CallbackState &state, const LldpObservation &obs) {
        const std::string key = resolveLldpIdentity(obs.remoteChassisId, obs.remotePortId);
        if (key.empty()) {
            VLOG(1) << "LLDP neighbor on " << obs.localIfname << " — non-MAC identity, not cached";
        } else {
            std::unique_lock cacheLk(state.cacheMutex);
            auto &ifaceCache = state.byInterface[obs.localIfname];
            if (obs.event == ObservationEvent::Removed) {
                VLOG(1) << "LLDP cache erase: ifname=" << obs.localIfname << " key=" << key;
                ifaceCache.erase(key);
            } else {
                VLOG(1) << "LLDP cache insert: ifname=" << obs.localIfname << " key=" << key;
                ifaceCache[key] = CachedLldpNeighbor{obs.remoteChassisId, obs.remotePortId, obs.remoteSystemName};
            }
            state.cacheGeneration.fetch_add(1, std::memory_order_release);
        }
        if (state.downstream)
            state.downstream(obs);
    }
} // anonymous namespace

// ---------------------------------------------------------------------------
// LldpdSource::Impl
// ---------------------------------------------------------------------------

class LldpdSource::Impl {
public:
    Impl(LldpSourceConfig config, LldpObservationCallback cb)
        : callbackState_(
            std::make_shared<CallbackState>(std::move(config), std::move(cb))) {
    }

    ~Impl() {
        try {
            stop();
        } catch (...) {
            LOG(ERROR) << "LldpdSource::Impl::~Impl: exception during stop (ignored)";
        }
    }

    bool start() {
        std::unique_lock lk(lifecycleMutex_);
        if (state_ == State::Running || state_ == State::Starting) return true;
        // pendingDrain_ means abandoned leases are still in flight from a
        // timed-out refreshAll(); reopening admission now would let them
        // coexist with fresh watch callbacks and break the drain
        // accounting. Caller must stop() (or drop the source) first.
        if (state_ != State::Stopped || pendingDrain_) return false;
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

    void stop() {
        // 1. Claim stopping under lifecycleMutex_
        std::unique_lock lk(lifecycleMutex_);
        // pendingDrain_ means a refreshAll() timed out with leases still
        // outstanding: state_ is Stopped but the drain postcondition has
        // not been met, so stop() must still run it.
        if (state_ == State::Stopped && !pendingDrain_) return;
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
            callbackState_->cv.wait(cbLk, [this] { return callbackState_->activeCount == 0; });
            callbackState_->cacheGeneration.fetch_add(1, std::memory_order_release);
        }

        // 7. Clear cache only after drain completes
        {
            std::unique_lock cacheLk(callbackState_->cacheMutex);
            callbackState_->byInterface.clear();
            callbackState_->cacheGeneration.fetch_add(1, std::memory_order_release);
        }

        // 8. Commit stopped and notify lifecycle waiters
        lk.lock();
        state_ = State::Stopped;
        pendingDrain_ = false;
        lk.unlock();
        lifecycleCv_.notify_all();

        LOG(INFO) << "LldpdSource stopped";
    }

    [[nodiscard]] bool isRunning() const {
        std::unique_lock lk(lifecycleMutex_);
        return state_ == State::Running;
    }

    /**
     * @brief Reconnect: close old watch, drain old callbacks, reopen with new
     *        watch.  If the reconnect fails, transitions to Stopped.
     */
    void refreshAll() {
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

        // 5. Drain old callbacks — BOUNDED. See kRefreshDrainTimeout.
        bool drained = false;
        {
            std::unique_lock cbLk(callbackState_->mtx);
            drained = callbackState_->cv.wait_for(
                cbLk, kRefreshDrainTimeout,
                [this] { return callbackState_->activeCount == 0; });
        }
        if (!drained) {
            // Abandon the reconnect rather than block shutdown. Admission
            // stays closed, the cache is left intact (the outstanding
            // callback may still be mutating it), and the source goes
            // Stopped so the runtime's next tick() drops and re-acquires
            // the observer. pendingDrain_ keeps stop()'s drain obligation
            // alive for the leases we did not wait out.
            LOG(ERROR) << "LldpdSource: reconnect drain timed out; abandoning refresh";
            lk.lock();
            state_ = State::Stopped;
            pendingDrain_ = true;
            lk.unlock();
            lifecycleCv_.notify_all();
            return;
        }

        // 6. Snapshot old cache for post-reconnect reconciliation
        NeighborCache oldCache;
        {
            std::unique_lock cacheLk(callbackState_->cacheMutex);
            oldCache = std::move(callbackState_->byInterface);
            callbackState_->byInterface.clear();
        }

        // 7. Reopen admission
        openAdmission(*callbackState_);

        // 8. Create new watch, re-enumerate, reconcile removals
        const bool ok = makeWatch();
        if (ok) {
            enumerateInitialNeighbors();
            reconcileAfterRefresh(std::move(oldCache));
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

    void refreshInterface(const std::string & /*ifname*/) {
    }

    /**
     * @brief Flush all cached neighbors for a removed interface.
     *
     * Holds one admission lease for the entire batch so stop() cannot
     * interleave between partial removals.  If admission is closed
     * (stop/refresh in progress) the batch is discarded.
     */
    void removeInterface(const std::string &ifname) {
        auto lease = tryAcquireLease(callbackState_);
        if (!lease) return; // stop/refresh in progress; discard batch

        std::vector<CachedLldpNeighbor> toRemove;
        {
            std::unique_lock cacheLk(callbackState_->cacheMutex);
            auto it = callbackState_->byInterface.find(ifname);
            if (it == callbackState_->byInterface.end()) return;
            for (const auto &[_, entry]: it->second)
                toRemove.push_back(entry);
            callbackState_->byInterface.erase(it);
            callbackState_->cacheGeneration.fetch_add(1, std::memory_order_release);
        }

        for (const auto &entry: toRemove) {
            deliver(makeLldpObservation(ifname, ObservationEvent::Removed, entry));
        }
        // lease released here, decrementing activeCount
    }

    /**
 * @brief Build a batch under cacheMutex, then deliver it unlocked.
 *
 * @param select        Invoked with the locked cache; appends to `out`.
 * @param generationGuard When true, abandon the remainder of the batch if
 *        the cache moves mid-delivery. Required for keepalives, which
 *        would otherwise resurrect a candidate removed between snapshot
 *        and emit. NOT wanted for removals: a Removed is still correct
 *        even if the cache has since changed, and dropping one would
 *        strand the candidate until ageout.
 *
 * Precondition: caller holds a CallbackLease.
 */
    template<typename Select>
    void emitBatch(Select &&select, bool generationGuard) {
        std::vector<LldpObservation> batch;
        std::uint64_t generation = 0;
        {
            std::unique_lock cacheLk(callbackState_->cacheMutex);
            generation = callbackState_->cacheGeneration.load(std::memory_order_acquire);
            select(callbackState_->byInterface, batch);
        }

        for (std::size_t i = 0; i < batch.size(); ++i) {
            if (generationGuard &&
                callbackState_->cacheGeneration.load(std::memory_order_acquire) != generation) {
                VLOG(1) << "emitBatch: cache mutated during delivery; dropping "
                        << (batch.size() - i) << " remaining observation(s)";
                return;
            }
            deliver(batch[i]);
        }
    }

    void deliver(const LldpObservation &obs) const {
        if (callbackState_->downstream) callbackState_->downstream(obs);
    }

    /**
     * @brief Re-emit every cached neighbor as a keepalive Present.
     *
     * The batch is built under cacheMutex, then delivered after the lock is
     * released. Does NOT stamp lastWatchEventAt — keepalives must not count
     * as backend liveness for the watchdog.
     */
    void reassertAll() {
        auto lease = tryAcquireLease(callbackState_);
        if (!lease) return; // stop/refresh in progress; keepalive dropped

        emitBatch([](const NeighborCache &cache, std::vector<LldpObservation> &out) {
            for (const auto &[ifname, neighbors]: cache)
                for (const auto &[_, entry]: neighbors)
                    out.push_back(makeLldpObservation(
                        ifname, ObservationEvent::Present, entry, /*keepalive=*/true));
        }, /*generationGuard=*/true);
    }

    void submitNeighborChangeForTest(std::string_view ifname,
                                     ObservationEvent event,
                                     std::optional<std::string> chassisId,
                                     std::optional<std::string> portId,
                                     std::optional<std::string> systemName) {
        auto lease = tryAcquireLease(callbackState_);
        if (!lease) return;

        const auto obs = makeLldpObservation(ifname, event, std::move(chassisId),std::move(portId), std::move(systemName));

        // Deliberately NOT liveness-stamped: the seam is not backend contact.
        cacheAndForward(*callbackState_, obs);
        // lease released here
    }

    /**
    * @brief Emit Removed for neighbors present before a reconnect but not
    *        re-observed during re-enumeration.
    *
    * Precondition: admission open; called after enumerateInitialNeighbors()
    * so callbackState_->byInterface holds the fresh set. Enumeration already
    * emitted Present for everything currently known; this pass emits the
    * removals the old silent cache clear used to swallow.
    */
    void reconcileAfterRefresh(NeighborCache oldCache) {
        auto lease = tryAcquireLease(callbackState_);
        if (!lease) return;

        std::vector<LldpObservation> removals;
        {
            std::unique_lock cacheLk(callbackState_->cacheMutex);
            for (const auto &[ifname, neighbors]: oldCache) {
                const auto freshIt = callbackState_->byInterface.find(ifname);
                for (const auto &[key, entry]: neighbors) {
                    const bool reSeen =
                            freshIt != callbackState_->byInterface.end() &&
                            freshIt->second.contains(key);
                    if (reSeen) continue;

                    LldpObservation obs;
                    obs.observedAt = std::chrono::steady_clock::now();
                    obs.kind = ObservationKind::Lldp;
                    obs.localIfname = ifname;
                    obs.event = ObservationEvent::Removed;
                    obs.remoteChassisId = entry.rawChassisId;
                    obs.remotePortId = entry.rawPortId;
                    obs.remoteSystemName = entry.rawSystemName;
                    removals.push_back(std::move(obs));
                }
            }
        }

        for (const auto &obs: removals) {
            if (callbackState_->downstream) callbackState_->downstream(obs);
        }
        // lease released here
    }

    /**
     * @brief Cheap lldpctl round-trip probing backend connectivity.
     *
     * Uses a separate short-lived connection; touches no watch/cache state,
     * so it is safe to call from the runtime tick thread concurrently with
     * watch callbacks.
     */
    [[nodiscard]] bool isBackendAlive() {
        try {
            BoundedLldpConnection probe(resolvedCtlPath(), kProbeConnectTimeout, kProbeIoTimeout);
            return probe.QueryInterfacesOk();
        } catch (const std::exception &e) {
            VLOG(1) << "LLDP backend probe failed: " << e.what();
            return false;
        } catch (...) {
            // Never let a probe failure kill the supervision worker.
            return false;
        }
    }

    [[nodiscard]] std::chrono::steady_clock::time_point lastEventAt() const {
        return callbackState_->lastWatchEventAt.load(std::memory_order_acquire);
    }

    void openAdmissionForTest() {
        openAdmission(*callbackState_);
    }

    void closeAdmissionAndDrainForTest() {
        closeAdmissionAndDrain(*callbackState_);
    }

private:
    /// Resolved lldpd control socket for bounded connections.
    [[nodiscard]] std::string resolvedCtlPath() const {
        return callbackState_->config.ctlSocketPath.empty()
                   ? ::lldpctl_get_default_transport()
                   : callbackState_->config.ctlSocketPath;
    }

    /**
     * @brief Invoke @p fn for each interface atom over a bounded connection.
     *
     * Callback form on purpose: atoms alias the connection non-owningly, so
     * they must never outlive this function — a returned list (the previous
     * form) could escape the bounded scope with no compiler help.
     */
    template<typename Fn>
    static void forEachInterfaceBounded(BoundedLldpConnection &bounded, Fn &&fn) {
        const std::shared_ptr<lldpctl_conn_t> aliased(bounded.connection(), [](lldpctl_conn_t *) {
        });

        lldpctl_atom_t *raw = ::lldpctl_get_interfaces(bounded.connection());
        if (!raw) {
            throw std::system_error(
                make_error_code(lldpctl_last_error(bounded.connection())),
                "forEachInterfaceBounded: lldpctl_get_interfaces failed");
        }
        struct AtomDecRef {
            void operator()(lldpctl_atom_t *a) const { ::lldpctl_atom_dec_ref(a); }
        };
        const std::unique_ptr<lldpctl_atom_t, AtomDecRef> interfaces(raw);

        lldpctl_atom_t *atom = nullptr;
        lldpctl_atom_foreach(interfaces.get(), atom) {
            lldpcli::LldpAtom iface(atom, true, aliased);
            fn(iface);
        }
    }

    bool makeWatch() {
        // Capture only a weak_ptr — the watch callback must not retain ownership
        // of the Impl or the shared CallbackState.
        std::weak_ptr<CallbackState> weakState = callbackState_;

        try {
            // BoundedLldpWatch, not lldpcli::LldpWatch: the latter builds its
            // own unbounded connection, and its subscribe round-trip would hang
            // this thread (start(), or refreshAll() on the supervision thread)
            // against a wedged lldpd.
            watch_ = std::make_unique<BoundedLldpWatch>(
                resolvedCtlPath(), kProbeConnectTimeout, kProbeIoTimeout,
                [weakState](std::string_view ifname,
                            lldpctl_change_t change,
                            const lldpcli::LldpAtom & /*interface*/,
                            const lldpcli::LldpAtom &neighbor) {
                    // Lock weak_ptr — if Impl is destroyed this is a no-op
                    auto state = weakState.lock();
                    if (!state) return;

                    auto lease = tryAcquireLease(state);
                    if (!lease) return; // admission closed

                    try {
                        dispatchChange(*state, ifname, change, neighbor);
                    } catch (const std::exception &e) {
                        LOG(ERROR) << "LldpdSource: watch callback exception: " << e.what();
                    } catch (...) {
                        LOG(ERROR) << "LldpdSource: watch callback unknown exception";
                    }
                    // lease released here, decrementing activeCount
                });
            return true;
        } catch (const std::exception &e) {
            LOG(ERROR) << "Lldpd watch creation failed: " << e.what();
            return false;
        } catch (...) {
            LOG(ERROR) << "Lldpd watch creation failed: unknown exception";
            return false;
        }
    }

    void enumerateInitialNeighbors() {
        // Precondition: admission is open; lifecycleMutex_ NOT held here.
        // Uses BoundedLldpConnection rather than lldpcli::LldpCtl: this runs
        // on the supervision (tick) thread via refreshAll(), and the default
        // lldpctl transport has no timeout, so a hung lldpd would block the
        // supervision worker and in turn delay ObservationService::stop().
        try {
            BoundedLldpConnection bounded(resolvedCtlPath(),
                                          kProbeConnectTimeout, kProbeIoTimeout);

            bool aborted = false;
            forEachInterfaceBounded(bounded, [&](const lldpcli::LldpAtom &iface) {
                if (aborted) return;

                auto ifname = iface.GetValue<std::string>(lldpctl_k_interface_name);
                if (!ifname) return;

                if (!callbackState_->config.watchedInterfaces.empty()) {
                    auto it = std::find(callbackState_->config.watchedInterfaces.begin(),
                                        callbackState_->config.watchedInterfaces.end(),
                                        *ifname);
                    if (it == callbackState_->config.watchedInterfaces.end()) return;
                }

                auto port = iface.GetPort();
                auto neighbors = port.GetAtomList(lldpctl_k_port_neighbors);

                for (const auto &nb: neighbors) {
                    auto lease = tryAcquireLease(callbackState_);
                    if (!lease) {
                        // stop/refresh began during enumeration — stop the
                        // whole walk, not just this interface.
                        aborted = true;
                        return;
                    }
                    try {
                        dispatchChange(*callbackState_, *ifname, lldpctl_c_added, nb);
                    } catch (const std::exception &e) {
                        LOG(ERROR) << "LLDP initial enumeration dispatch error: " << e.what();
                    }
                }
            });

            if (aborted) {
                VLOG(1) << "LLDP initial enumeration aborted (admission closed)";
                return;
            }

            VLOG(1) << "LLDP initial enumeration complete";
            // A completed enumeration proves backend connectivity even when
            // zero neighbors were found (dispatchChange stamps per neighbor).
            // Only stamp on a full walk: an aborted one proves nothing.
            callbackState_->lastWatchEventAt.store(std::chrono::steady_clock::now(),
                                                   std::memory_order_release);
        } catch (const std::exception &e) {
            LOG(ERROR) << "LLDP initial enumeration failed: " << e.what();
        }
    }

    /**
     * @brief Process a single parsed change event against the given state.
     *
     * Caller must hold a valid CallbackLease (keeping activeCount > 0).
     * Cache locks are released before the downstream callback.
     */
    static void dispatchChange(CallbackState &state,
                               std::string_view ifname,
                               lldpctl_change_t change,
                               const lldpcli::LldpAtom &neighbor) {
        // Backend-originated contact — liveness stamp. reassertAll() and
        // removeInterface() bypass this function on purpose.
        state.lastWatchEventAt.store(std::chrono::steady_clock::now(),
                                     std::memory_order_release);

        if (!state.config.watchedInterfaces.empty()) {
            auto it = std::find(state.config.watchedInterfaces.begin(),
                                state.config.watchedInterfaces.end(),
                                ifname);
            if (it == state.config.watchedInterfaces.end()) return;
        }

        if (change == lldpctl_c_deleted && !state.config.emitRemovals) return;

        LldpObservation obs;
        obs.observedAt = std::chrono::steady_clock::now();
        obs.kind = ObservationKind::Lldp;
        obs.localIfname = std::string(ifname);
        obs.event = (change == lldpctl_c_deleted)
                        ? ObservationEvent::Removed
                        : ObservationEvent::Present;

        if (auto chassisId = neighbor.GetValue<std::string>(lldpctl_k_chassis_id))
            obs.remoteChassisId = *chassisId;
        if (auto portId = neighbor.GetValue<std::string>(lldpctl_k_port_id))
            obs.remotePortId = *portId;
        if (auto chassisName = neighbor.GetValue<std::string>(lldpctl_k_chassis_name))
            obs.remoteSystemName = *chassisName;

        cacheAndForward(state, obs);
    }

    enum class State { Stopped, Starting, Running, Refreshing, Stopping };

    std::shared_ptr<CallbackState> callbackState_;
    std::unique_ptr<BoundedLldpWatch> watch_;

    mutable std::mutex lifecycleMutex_;
    std::condition_variable lifecycleCv_;
    State state_ = State::Stopped;
    /// Set when refreshAll() gave up on the lease drain; forces stop() to
    /// complete the drain even though state_ is already Stopped.
    bool pendingDrain_ = false;
};

// ---------------------------------------------------------------------------
// LldpdSource public interface — thin delegation to Impl
// ---------------------------------------------------------------------------

LldpdSource::LldpdSource(LldpSourceConfig config, LldpObservationCallback cb)
    : impl_(std::make_unique<Impl>(std::move(config), std::move(cb))) {
}

LldpdSource::~LldpdSource() = default;

bool LldpdSource::start() { return impl_->start(); }
void LldpdSource::stop() { impl_->stop(); }
bool LldpdSource::isRunning() const { return impl_->isRunning(); }
void LldpdSource::refreshAll() { impl_->refreshAll(); }
void LldpdSource::refreshInterface(const std::string &ifname) { impl_->refreshInterface(ifname); }
void LldpdSource::removeInterface(const std::string &ifname) { impl_->removeInterface(ifname); }

void LldpdSource::submitNeighborChangeForTest(std::string_view ifname,
                                              ObservationEvent event,
                                              std::optional<std::string> chassisId,
                                              std::optional<std::string> portId,
                                              std::optional<std::string> systemName) {
    impl_->submitNeighborChangeForTest(ifname, event, std::move(chassisId), std::move(portId), std::move(systemName));
}

void LldpdSource::reassertAll() { impl_->reassertAll(); }
bool LldpdSource::isBackendAlive() const { return impl_->isBackendAlive(); }
std::chrono::steady_clock::time_point LldpdSource::lastEventAt() const { return impl_->lastEventAt(); }

void LldpdSource::openAdmissionForTest() { impl_->openAdmissionForTest(); }
void LldpdSource::closeAdmissionAndDrainForTest() { impl_->closeAdmissionAndDrainForTest(); }
} // namespace RSCGroup
