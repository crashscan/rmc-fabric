//
// Created by vvass on 24-Jul-26.
//
/**
 * @file LldpdSource.h
 * @brief LLDP source backed by the lldpd daemon via liblldpctl.
 *
 * Wraps lldpcli::LldpWatch for push-based change notifications.
 * Implements ILldpSource — refresh methods are advisory (reconnect).
 *
 * @section callback-safety Callback-drain safety
 * The external LldpWatch callback captures only a weak_ptr to the internal
 * CallbackState.  stop() closes the admission gate, destroys the watch
 * handle, and waits for all active callback leases to drain before clearing
 * cache state or returning.
 *
 * Postcondition of stop(): no LLDP callback is executing; no new callback
 * can be admitted; cache is cleared; the watch handle is released.
 *
 * @section reentrancy Reentrancy
 * Downstream observation callbacks must not synchronously call stop(),
 * refreshAll(), or destroy the LldpdSource — doing so from within a
 * callback is a programming error.  Destructors are non-throwing; misuse
 * (destruction during a callback) is logged.
 *
 * @section limitations v1 Limitations
 * Only MAC-like LLDP identities are cached and forwarded to the
 * observation model. Non-MAC chassis IDs (hostnames, network
 * addresses, local identifiers) are passed through as observations
 * but not cached for interface-flush purposes. Full non-MAC identity
 * support is deferred to v2.
 */
#pragma once
#include "ILldpSource.h"
#include "LldpObserverTypes.h"
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace RSCGroup {
class LldpdSource : public ILldpSource {
public:
    LldpdSource(LldpSourceConfig config, LldpObservationCallback cb);

    ~LldpdSource() override;

    bool start() override;

    /**
     * @brief Stop and drain.
     *
     * Postcondition: no LLDP watch callback is executing; no new callback
     * can be admitted; cache state is cleared; watch handle is released.
     * Safe to call multiple times (idempotent).
     */
    void stop() override;

    [[nodiscard]] bool isRunning() const override;

    void refreshAll() override;

    void refreshInterface(const std::string &ifname) override;

    void removeInterface(const std::string &ifname) override;

    /**
     * @brief Re-emit all cached neighbors as keepalive Present observations.
     *
     * Holds one admission lease for the whole batch (mirrors
     * removeInterface). The cache lock is released before downstream
     * delivery. Does not stamp liveness. No-op when admission is closed.
     */
    void reassertAll() override;

    /// Probes backend connectivity over a separate short-lived connection.
    /// const because it mutates no source state — the probe owns its own
    /// socket and touches neither the watch nor the cache.
    [[nodiscard]] bool isBackendAlive() const override;

    [[nodiscard]] std::chrono::steady_clock::time_point lastEventAt() const override;

    /**
     * @brief Test seam: inject a parsed neighbor change directly through the
     *        admission gate, without a running lldpd daemon.
     *
     * The injection is dispatched through the same admission/callback barrier
     * as live watch callbacks.  If admission is closed the call is a no-op.
     *
     * This method is intended for unit tests only; do not call from
     * production code.
     */
    void submitNeighborChangeForTest(std::string_view ifname,
                                     ObservationEvent event,
                                     std::optional<std::string> chassisId,
                                     std::optional<std::string> portId,
                                     std::optional<std::string> systemName);

    /**
     * @brief Test seam: open the callback admission gate without a backend.
     *
     * While open, submitNeighborChangeForTest / reassertAll /
     * removeInterface run exactly as with a live backend, enabling
     * cache-level unit tests without lldpd. Does not change lifecycle
     * state and does not stamp liveness. Not for production use.
     */
    void openAdmissionForTest();

    /**
     * @brief Test seam: close admission and drain active leases.
     */
    void closeAdmissionAndDrainForTest();

    /**
      * @brief Test seam: run the post-reconnect reconciliation pass directly.
      *
      * refreshAll() reaches reconcileAfterRefresh() only after a successful
      * makeWatch() + enumerateInitialNeighbors(), both of which require a live
      * lldpd. This seam supplies the pre-reconnect snapshot directly so the
      * removal diff — and its deliberately unguarded delivery, the only
      * generationGuard=false path in the source — can be exercised without a
      * daemon.
      *
      * @param oldNeighbors Pre-reconnect neighbours as (ifname, chassisId,
      *        portId). Entries whose identity is also present in the current
      *        cache are treated as re-seen and produce no Removed. Entries
      *        with a non-MAC identity are skipped, matching the caching rule
      *        in cacheAndForward().
      *
      * Precondition: admission is open (openAdmissionForTest()). If admission
      * is closed the call is a no-op, exactly as the real path would be.
      *
      * Unit tests only; do not call from production code.
      */
    void reconcileAfterRefreshForTest(const std::vector<std::tuple<std::string, std::string, std::string> > &oldNeighbors);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace RSCGroup