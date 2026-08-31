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
#include <memory>
#include <optional>
#include <string>
#include <string_view>

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
    void refreshInterface(const std::string& ifname) override;
    void removeInterface(const std::string& ifname) override;

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

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace RSCGroup