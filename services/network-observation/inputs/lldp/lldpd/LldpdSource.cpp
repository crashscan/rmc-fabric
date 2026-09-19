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
#include <vector>
#include <optional>

#include "BoundedLldpConnection.h"
#include "BoundedLldpWatch.h"
#include "LldpObservationFactory.h"
#include "LldpNeighborCache.h"

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

        /// Guards the callback region. Starts closed.
        thread_safe::admission_gate gate;

        /// Owns its own lock and generation counter; see LldpNeighborCache.
        LldpNeighborCache cache;
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

    void cacheAndForward(CallbackState &state, const LldpObservation &obs) {
        // Non-cacheable identities are still delivered — see the v1
        // limitation in LldpNeighborCache::apply().
        (void) state.cache.apply(obs);
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
        if (state_ != State::Stopped) return false;
        // Leases outstanding means a previous refreshAll() abandoned its
        // drain. Reopening admission now would let those callbacks coexist
        // with fresh watch callbacks against a cache stop() never cleared.
        // Caller must stop() (or drop the source) first.
        if (callbackState_->gate.active() != 0) {
            LOG(WARNING) << "LldpdSource: start() refused; leases outstanding";
            return false;
        }
        state_ = State::Starting;
        lk.unlock();

        // Open admission BEFORE creating watch so synchronous callbacks admitted
        callbackState_->gate.open();

        if (const bool ok = makeWatch(); !ok) {
            callbackState_->gate.close_and_drain();
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
        if (state_ == State::Stopping) {
            lifecycleCv_.wait(lk, [this] { return state_ == State::Stopped; });
            return;
        }
        if (state_ == State::Stopped) {
            // Already stopped, but a previous refreshAll() may have
            // abandoned its drain and left the cache populated. Completing
            // the drain and the clear here is what pendingDrain_ used to
            // force; doing it unconditionally removes the flag without
            // weakening the postcondition. Both operations are cheap no-ops
            // on a cleanly stopped source.
            lk.unlock();
            callbackState_->gate.close_and_drain();
            callbackState_->cache.clear();
            return;
        }
        state_ = State::Stopping;

        // 2. Close callback admission
        callbackState_->gate.close();

        // 3. Move watch handle out of shared state
        auto watchToDestroy = std::move(watch_);

        // 4. Release lifecycle mutex before any blocking work
        lk.unlock();

        // 5. Destroy watch outside the lifecycle mutex (may block briefly)
        watchToDestroy.reset();

        // 6. Wait for active callback leases to drain
        callbackState_->gate.drain();

        // 7. Clear cache only after drain completes
        callbackState_->cache.clear();

        // 8. Commit stopped and notify lifecycle waiters
        lk.lock();
        state_ = State::Stopped;
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
        callbackState_->gate.close();

        // 3. Move old watch out
        auto oldWatch = std::move(watch_);

        lk.unlock();

        // 4. Destroy old watch outside mutex
        oldWatch.reset();

        // 5. Drain old callbacks — BOUNDED. See kRefreshDrainTimeout.
        if (const bool drained = callbackState_->gate.drain(kRefreshDrainTimeout); !drained) {
            // Abandon the reconnect rather than block shutdown. Admission
            // stays closed, the cache is left intact (the outstanding
            // callback may still be mutating it), and the source goes
            // Stopped so the runtime's next tick() drops and re-acquires
            // the observer. pendingDrain_ keeps stop()'s drain obligation
            // alive for the leases we did not wait out.
            LOG(ERROR) << "LldpdSource: reconnect drain timed out; abandoning refresh";
            lk.lock();
            state_ = State::Stopped;
            lk.unlock();
            lifecycleCv_.notify_all();
            return;
        }

        // 6. Snapshot old cache for post-reconnect reconciliation
        NeighborCacheMap oldCache = callbackState_->cache.exchange({});

        // 7. Reopen admission
        callbackState_->gate.open();

        // 8. Create new watch, re-enumerate, reconcile removals
        const bool ok = makeWatch();
        if (ok) {
            enumerateInitialNeighbors();
            reconcileAfterRefresh(oldCache);
        } else {
            callbackState_->gate.close_and_drain();
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

    /// No-op by contract: lldpd is push-based and already notifies
    /// per-interface changes continuously. There is no per-interface query
    /// in lldpctl — the only resync available is the full reconnect in
    /// refreshAll(). See ILldpSource::refreshInterface.
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
        auto lease = callbackState_->gate.try_acquire();
        if (!lease) {
            return; // stop/refresh in progress; discard batch
        }

        // Flush returns the entries so delivery stays outside the cache
        // lock while the lease is still held.
        for (const auto &entry: callbackState_->cache.flushInterface(ifname)) {
            deliver(makeLldpObservation(ifname, ObservationEvent::Removed, entry));
        }
    }

    /**
     * @brief Build a keepalive batch from a cache snapshot and deliver it.
     *
     * Always generation-guarded: reassertAll() is now the only caller, and a
     * keepalive emitted after the cache moved would resurrect a candidate
     * removed between snapshot and emit. The former unguarded caller,
     * reconcileAfterRefresh(), no longer routes through here — a Removed
     * stays correct regardless of cache movement.
     *
     * `select` runs over a COPY, outside the cache lock. Costs one map copy
     * per keepalive cycle; buys a cache that never runs caller-supplied
     * code under its own mutex.
     */
    template<typename Select>
    void emitBatch(Select &&select) {
        auto [cache, generation] = callbackState_->cache.snapshot();
        std::vector<LldpObservation> batch;
        select(cache, batch);

        for (std::size_t i = 0; i < batch.size(); ++i) {
            if (callbackState_->cache.generation() != generation) {
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
     * Does NOT stamp lastWatchEventAt — keepalives must not count
     * as backend liveness for the watchdog.
     */
    void reassertAll() {
        const auto lease = callbackState_->gate.try_acquire();
        if (!lease) {
            return; // stop/refresh in progress; keepalive dropped
        }

        emitBatch([](const NeighborCacheMap &cache, std::vector<LldpObservation> &out) {
            for (const auto &[ifname, neighbors]: cache)
                for (const auto &[_, entry]: neighbors)
                    out.push_back(makeLldpObservation(ifname, ObservationEvent::Present, entry, true));
        });
    }

    void submitNeighborChangeForTest(std::string_view ifname,
                                     ObservationEvent event,
                                     std::optional<std::string> chassisId,
                                     std::optional<std::string> portId,
                                     std::optional<std::string> systemName) {
        const auto lease = callbackState_->gate.try_acquire();
        if (!lease) {
            return;
        }

        const auto obs = makeLldpObservation(ifname, event, std::move(chassisId), std::move(portId),
                                             std::move(systemName));

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
     *
     * Unguarded on purpose (generationGuard=false): a Removed stays correct
     * even if the cache moves mid-delivery, and abandoning the batch would
     * strand the candidate until candidateAgeout. Keepalives are the opposite
     * case — see reassertAll().
     */
    void reconcileAfterRefresh(const NeighborCacheMap &oldCache) {
        const auto lease = callbackState_->gate.try_acquire();
        if (!lease) {
            return;
        }
        const auto [fresh, _] = callbackState_->cache.snapshot();
        for (const auto &[ifname, entry]: diffRemovedNeighbors(oldCache, fresh)) {
            deliver(makeLldpObservation(ifname, ObservationEvent::Removed, entry));
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
    [[nodiscard]] bool isBackendAlive() const {
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

    [[nodiscard]] thread_safe::admission_gate &gate() {
        return callbackState_->gate;
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

                    const auto lease = state->gate.try_acquire();
                    if (!lease) {
                        return; // admission closed
                    }

                    try {
                        dispatchChange(*state, ifname, change, neighbor);
                    } catch (const std::exception &e) {
                        LOG(ERROR) << "LldpdSource: watch callback exception: " << e.what();
                    } catch (...) {
                        LOG(ERROR) << "LldpdSource: watch callback unknown exception";
                    }
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
            BoundedLldpConnection bounded(resolvedCtlPath(), kProbeConnectTimeout, kProbeIoTimeout);

            bool aborted = false;
            forEachInterfaceBounded(bounded, [&](const lldpcli::LldpAtom &iface) {
                if (aborted) return;

                auto ifname = iface.GetValue<std::string>(lldpctl_k_interface_name);
                if (!ifname) return;

                if (!callbackState_->config.watchedInterfaces.empty()) {
                    auto it = std::ranges::find(callbackState_->config.watchedInterfaces, *ifname);
                    if (it == callbackState_->config.watchedInterfaces.end()) return;
                }

                auto port = iface.GetPort();
                auto neighbors = port.GetAtomList(lldpctl_k_port_neighbors);

                for (const auto &nb: neighbors) {
                    const auto lease = callbackState_->gate.try_acquire();
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
            callbackState_->lastWatchEventAt.store(std::chrono::steady_clock::now(), std::memory_order_release);
        } catch (const std::exception &e) {
            LOG(ERROR) << "LLDP initial enumeration failed: " << e.what();
        }
    }

    /**
     * @brief Process a single parsed change event against the given state.
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

        // Hoisted so the factory receives owning optionals. GetValue returns
        // optional<string> by value; an absent field stays nullopt, which is
        // what the cache and the model both expect.
        std::optional<std::string> chassisId;
        std::optional<std::string> portId;
        std::optional<std::string> systemName;
        if (auto v = neighbor.GetValue<std::string>(lldpctl_k_chassis_id))
            chassisId = std::move(*v);
        if (auto v = neighbor.GetValue<std::string>(lldpctl_k_port_id))
            portId = std::move(*v);
        if (auto v = neighbor.GetValue<std::string>(lldpctl_k_chassis_name))
            systemName = std::move(*v);

        const auto obs = makeLldpObservation(
            ifname,
            change == lldpctl_c_deleted
                ? ObservationEvent::Removed
                : ObservationEvent::Present,
            std::move(chassisId), std::move(portId), std::move(systemName));

        cacheAndForward(state, obs);
    }

    enum class State { Stopped, Starting, Running, Refreshing, Stopping };

    std::shared_ptr<CallbackState> callbackState_;
    std::unique_ptr<BoundedLldpWatch> watch_;

    mutable std::mutex lifecycleMutex_;
    std::condition_variable lifecycleCv_;
    State state_ = State::Stopped;
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

thread_safe::admission_gate &LldpdSource::admissionGateForTest() {
    return impl_->gate();
}
} // namespace RSCGroup
